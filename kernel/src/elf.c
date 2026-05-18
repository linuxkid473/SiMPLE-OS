#include "elf.h"
#include "klog.h"
#include "vga.h"
#include "types.h"

#define USER_BASE  0x100000   /* where PT_LOAD segments land          */
#define USER_STACK 0x200000   /* top of the user stack (grows down)   */

/*
 * Kernel register state saved by launch_program() before the user stack switch.
 *
 * All callee-saved registers (EBP, EBX, ESI, EDI) plus ESP are captured so
 * that both exit paths (exit_trampoline via SYS_EXIT, and prog_cleanup via
 * _start's normal return) can fully restore exec_elf's stack frame, allowing
 * launch_program to return normally via `ret`.
 *
 * kernel_esp — points to the return address pushed by `call launch_program`
 *              so that a subsequent `ret` lands back in exec_elf.
 *
 * process_exited — set to 1 by SYS_EXIT (idt.c) or by prog_cleanup.
 *                  Not used internally for control flow; kept for any external
 *                  observer that cares whether the program has exited.
 */
uint32_t kernel_esp     = 0;
int      process_exited = 0;

static uint32_t saved_ebp = 0;
static uint32_t saved_ebx = 0;
static uint32_t saved_esi = 0;
static uint32_t saved_edi = 0;

/*
 * prog_cleanup — called when _start() returns without invoking SYS_EXIT.
 *
 * _start's `ret` pops this address (planted at user_sp) and jumps here.
 * We are still on the user stack.  Call klog for diagnostics, then restore
 * the full kernel register context saved by launch_program and `ret` back
 * into exec_elf.
 */
static void prog_cleanup(void) {
    klog("elf", "program returned without sys_exit — treating as exit(0)");
    process_exited = 1;
    __asm__ volatile(
        "movl %[di], %%edi\n\t"
        "movl %[si], %%esi\n\t"
        "movl %[bx], %%ebx\n\t"
        "movl %[bp], %%ebp\n\t"
        "movl %[sp], %%esp\n\t"   /* switch back to kernel stack        */
        "ret\n\t"                  /* pop return addr → back in exec_elf */
        :
        : [di]"m"(saved_edi), [si]"m"(saved_esi),
          [bx]"m"(saved_ebx), [bp]"m"(saved_ebp),
          [sp]"m"(kernel_esp)
        : "memory"
    );
    /* unreachable */
    while (1) __asm__ volatile("hlt");
}

/*
 * exit_trampoline — iret from isr_syscall's epilogue lands here when
 * SYS_EXIT has patched regs->eip.
 *
 * Entry state (after iret):
 *   - Ring 0, kernel CS (0x08)
 *   - EFLAGS restored from the original pushed value — IF=0 (this kernel
 *     never calls sti; the 8259A PIC is not remapped, so enabling interrupts
 *     would fire hardware IRQs at exception vectors → panic).
 *   - ESP = the user program's stack pointer (iret popped the 3-word frame)
 *
 * Execution:
 *   1. cli — belt-and-suspenders; IF is already 0 after iret.
 *   2. Restore all callee-saved registers from the globals saved by
 *      launch_program — this repairs exec_elf's stack frame that popa in
 *      the ISR epilogue clobbered.
 *   3. Restore kernel_esp and `ret` — lands back in exec_elf.
 *
 * Must be naked: no GCC prologue may touch the (invalid) user stack.
 * Basic asm inside naked: '%' is literal, not an operand escape.
 */
__attribute__((naked)) void exit_trampoline(void) {
    __asm__(
        "cli\n\t"                      /* keep IF=0; already 0 after iret    */
        "movl saved_edi, %edi\n\t"
        "movl saved_esi, %esi\n\t"
        "movl saved_ebx, %ebx\n\t"
        "movl saved_ebp, %ebp\n\t"
        "movl kernel_esp, %esp\n\t"    /* restore kernel stack               */
        "ret\n\t"                      /* pop return addr → back in exec_elf */
    );
}

/*
 * launch_program — saves the full kernel register context, switches to the
 * user stack, then jumps to the ELF entry point.
 *
 * On entry (cdecl):
 *   4(%esp) = entry   — ELF entry virtual address
 *   8(%esp) = user_sp — initial user stack pointer (below prog_cleanup)
 *
 * Returns when exit_trampoline or prog_cleanup restores kernel_esp and
 * executes `ret`, which pops the return address pushed by `call launch_program`
 * back in exec_elf.
 *
 * Must be naked (no GCC prologue modifies ESP before we capture it).
 * Must be noinline (guarantees cdecl stack layout for the arg reads).
 * Basic asm inside naked: '%' is literal.
 */
__attribute__((naked, noinline)) static void launch_program(uint32_t entry, uint32_t user_sp) {
    __asm__(
        "movl %esp, kernel_esp\n\t"   /* save kernel ESP (→ return addr)    */
        "movl %ebp, saved_ebp\n\t"
        "movl %ebx, saved_ebx\n\t"
        "movl %esi, saved_esi\n\t"
        "movl %edi, saved_edi\n\t"
        "movl 8(%esp), %eax\n\t"      /* user_sp (2nd arg)                  */
        "movl 4(%esp), %ecx\n\t"      /* entry   (1st arg)                  */
        "movl %eax, %esp\n\t"         /* switch to user stack               */
        "jmp *%ecx\n\t"               /* jump to _start; ret → prog_cleanup */
    );
}

/* =========================
   ELF VALIDATION
========================= */
int elf_validate(void* data) {
    uint8_t* e = (uint8_t*)data;

    if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F')
        return -1;

    if (e[4] != 1)   /* must be 32-bit */
        return -1;

    if (((Elf32_Ehdr*)data)->e_type != ET_EXEC)
        return -1;

    return 0;
}

/* =========================
   EXEC ELF
========================= */
int exec_elf(void* data) {
    if (elf_validate(data) != 0) {
        vga_write_line("Invalid ELF");
        return -1;
    }

    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)data;
    Elf32_Phdr* phdr = (Elf32_Phdr*)((uint8_t*)data + ehdr->e_phoff);
    uint32_t    base = 0;

    /* Compute load bias: shift all vaddrs so the first PT_LOAD lands at USER_BASE */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            base = USER_BASE - phdr[i].p_vaddr;
            break;
        }
    }

    /* Copy segments into memory */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uint8_t* dest = (uint8_t*)(phdr[i].p_vaddr + base);
        uint8_t* src  = (uint8_t*)data + phdr[i].p_offset;

        for (uint32_t j = 0; j < phdr[i].p_filesz; j++)
            dest[j] = src[j];
        for (uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++)
            dest[j] = 0;  /* zero BSS */
    }

    uint32_t entry = ehdr->e_entry + base;

    klog("elf", "launching program");

    process_exited = 0;

    /*
     * Plant prog_cleanup as the return address below _start's initial SP.
     * If _start does `ret` without calling sys_exit, the CPU pops this
     * address and jumps to prog_cleanup, which restores the kernel context.
     */
    uint32_t user_sp = USER_STACK - 4;
    *(uint32_t*)user_sp = (uint32_t)prog_cleanup;

    /*
     * Switch to the user stack and jump to _start.
     * launch_program saves the full kernel register context (ESP, EBP, EBX,
     * ESI, EDI) before the stack switch.  Both exit paths restore that
     * context and execute `ret`, returning here normally.
     *
     * This avoids the two bugs of the previous computed-goto approach:
     *   1. GCC -O2 placed &&exit_point at the wrong basic block.
     *   2. `popa` in the ISR epilogue clobbered EBP, corrupting exec_elf's
     *      frame pointer and making the function epilogue compute garbage ESP.
     */
    launch_program(entry, user_sp);

    /*
     * Both exit paths (SYS_EXIT trampoline and prog_cleanup) restore the
     * kernel context and ret here.  The kernel stack and all callee-saved
     * registers are valid again.
     */
    klog("elf", "program exited");
    return 0;
}
