#include "elf.h"
#include "gdt.h"
#include "klog.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "vga.h"
#include "types.h"

/*
 * User address space layout:
 *
 *   0x100000 – ~0x1FFFFF  kernel code + data + BSS  (supervisor)
 *   0x200000 – 0x2FFFFF   kmalloc heap               (supervisor)
 *   0x300000 – 0x3EFFFF   user ELF code/data/BSS     (USER_BASE, user-accessible)
 *   0x3F0000 – 0x3FFFC7   user stack (grows down)    (user-accessible)
 *   0x3FFFC8 – 0x3FFFFF   exit stub + stack frame    (user-accessible)
 *
 * USER_BASE and USER_STACK come from elf.h; must match user/linker.ld and paging.c.
 */

/*
 * The ISR kernel stack is now managed per-process in process.c (proc_kstacks[]).
 * proc_register_initial() calls tss_set_esp0() with proc_kstacks[0] before
 * launch_ring3(), so no separate kstack is needed here.
 */

/*
 * Context saved by launch_ring3() so that exit_trampoline can restore it
 * and ret back into exec_elf.
 */
uint32_t kernel_esp     = 0;
int      process_exited = 0;
uint32_t saved_ebp      = 0;
uint32_t saved_ebx      = 0;
uint32_t saved_esi      = 0;
uint32_t saved_edi      = 0;

/*
 * exit_trampoline — entered via iret when SYS_EXIT (or a fatal user fault)
 * patches the iret frame to CS=0x08 / EIP=exit_trampoline.
 *
 * At entry we are in ring0 but DS/ES/FS/GS still hold the user data selector
 * (0x23).  In a flat model CPL=0 lets us access supervisor pages through any
 * selector, but we restore the kernel selector anyway for cleanliness.
 *
 * Then we restore the full kernel register context saved by launch_ring3 and
 * `ret` back into exec_elf.
 */
__attribute__((naked)) void exit_trampoline(void) {
    __asm__(
        "cli\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movl saved_edi, %%edi\n\t"
        "movl saved_esi, %%esi\n\t"
        "movl saved_ebx, %%ebx\n\t"
        "movl saved_ebp, %%ebp\n\t"
        "movl kernel_esp, %%esp\n\t"   /* switch to exec_elf's kernel stack */
        "ret\n\t"                       /* pop saved return addr → exec_elf */
        : : : "memory"
    );
}

/*
 * launch_ring3 — saves kernel context, then irets into ring3 user code.
 *
 * cdecl on entry (naked, so no prologue):
 *   4(%esp) = entry   — user EIP
 *   8(%esp) = user_sp — user ESP
 *
 * iret frame pushed onto current kernel stack:
 *   [SS=0x23, ESP=user_sp, EFLAGS(IF=0), CS=0x1B, EIP=entry]
 *
 * Returns to exec_elf only when exit_trampoline fires (via SYS_EXIT or
 * a fatal user-space exception patching the iret frame).
 */
__attribute__((naked, noinline)) static void
launch_ring3(uint32_t entry   __attribute__((unused)),
             uint32_t user_sp __attribute__((unused))) {
    __asm__(
        /* save kernel context */
        "movl %%esp, kernel_esp\n\t"
        "movl %%ebp, saved_ebp\n\t"
        "movl %%ebx, saved_ebx\n\t"
        "movl %%esi, saved_esi\n\t"
        "movl %%edi, saved_edi\n\t"

        /* read cdecl args before we change segments */
        "movl 4(%%esp), %%ecx\n\t"    /* entry   */
        "movl 8(%%esp), %%edx\n\t"    /* user_sp */

        /* switch data segments to user (DPL=3 flat) */
        "movw $0x23, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"

        /* build ring3 iret frame on the current kernel stack */
        "pushl $0x23\n\t"             /* SS  = user data (0x20 | RPL=3) */
        "pushl %%edx\n\t"             /* ESP = user_sp                  */
        "pushfl\n\t"                  /* EFLAGS                         */
        "orl $0x200, (%%esp)\n\t"     /* set IF — enable timer preemption in ring3 */
        "pushl $0x1B\n\t"             /* CS  = user code (0x18 | RPL=3) */
        "pushl %%ecx\n\t"             /* EIP = entry                    */

        "iret\n\t"
        : : : "memory"
    );
}

/* -------------------------------------------------------------------------
   ELF validation
   ---------------------------------------------------------------------- */
int elf_validate(void *data) {
    uint8_t *e = (uint8_t *)data;
    if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F')
        return -1;
    if (e[4] != 1)
        return -1;
    if (((Elf32_Ehdr *)data)->e_type != ET_EXEC)
        return -1;
    return 0;
}

/* -------------------------------------------------------------------------
   ELF execution entry point
   ---------------------------------------------------------------------- */
int exec_elf(void *data) {
    if (elf_validate(data) != 0) {
        vga_write_line("ELF: invalid");
        return -1;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)data;
    Elf32_Phdr *phdr = (Elf32_Phdr *)((uint8_t *)data + ehdr->e_phoff);

    /* Compute load bias so the first PT_LOAD lands at USER_BASE. */
    uint32_t base = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            base = USER_BASE - phdr[i].p_vaddr;
            break;
        }
    }

    /* Bounds check: refuse anything outside user space. */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t dest_start = phdr[i].p_vaddr + base;
        uint32_t dest_end   = dest_start + phdr[i].p_memsz;
        if (dest_start < USER_BASE || dest_end > USER_STACK) {
            klog_hex("elf", "unsafe load dest", dest_start);
            vga_write_line("ELF: load region outside user space — refused");
            return -1;
        }
    }

    /* Copy segments and zero BSS. */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint8_t *dest = (uint8_t *)(phdr[i].p_vaddr + base);
        uint8_t *src  = (uint8_t *)data + phdr[i].p_offset;
        for (uint32_t j = 0; j < phdr[i].p_filesz; j++) dest[j] = src[j];
        for (uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) dest[j] = 0;
    }

    uint32_t entry = ehdr->e_entry + base;

    /*
     * Plant a tiny exit stub in the user stack area so that if _start
     * returns without calling SYS_EXIT, the CPU executes:
     *   mov $2, %eax  ; SYS_EXIT
     *   xor %ecx, %ecx
     *   int $0x80
     * The stub is placed 32 bytes below the stack top.
     */
    static const uint8_t exit_stub[] = {
        0xB8, 0x02, 0x00, 0x00, 0x00,  /* mov $2, %eax  */
        0x31, 0xC9,                      /* xor %ecx,%ecx */
        0xCD, 0x80,                      /* int $0x80     */
        0xF4                             /* hlt (safety)  */
    };
    uint32_t stub_addr = USER_STACK - 32;
    uint8_t *stub_ptr  = (uint8_t *)stub_addr;
    for (uint32_t i = 0; i < sizeof(exit_stub); i++) stub_ptr[i] = exit_stub[i];

    /* Plant stub address as _start's return address. */
    uint32_t user_sp = USER_STACK - 4 - 32;   /* below the stub */
    *(uint32_t *)(user_sp) = stub_addr;

    klog_hex("elf", "entry",    entry);
    klog_hex("elf", "user_sp",  user_sp);
    klog_hex("elf", "stub",     stub_addr);
    klog("elf", "launching ring3 program");

    /*
     * Register this as the initial process (slot 0).
     * proc_register_initial() sets TSS.esp0 to proc_kstacks[0] and sets
     * current_proc=0 so syscall handlers can find the right fd_table.
     */
    proc_register_initial(paging_get_page_dir(), (fd_table_t *)0);

    process_exited = 0;

    /*
     * iret into ring3.  Returns here only when exit_trampoline fires
     * (via SYS_EXIT or a fatal user fault).
     */
    launch_ring3(entry, user_sp);

    klog("elf", "program exited");
    return 0;
}
