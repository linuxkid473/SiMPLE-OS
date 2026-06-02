#include "elf.h"
#include "gdt.h"
#include "klog.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "signal.h"
#include "string.h"
#include "vga.h"
#include "types.h"

/*
 * User address space layout:
 *
 *   0x100000 – _kernel_end kernel code + data + BSS  (supervisor)
 *   _kernel_end – ~0x3F0000 kmalloc heap             (supervisor)
 *   0x300000 – 0x3FFDE7   user ELF code/data/BSS + stack (USER_BASE, user-accessible)
 *   0x3FFFE0            USER_INITIAL_SP (stack grows down from here)
 *   0x3FFFE8 – 0x3FFFEF  SIG_TRAMPOLINE_ADDR (above initial SP, never clobbered)
 *   0x3FFFF0 – 0x3FFFF9  EXIT_STUB_ADDR      (above initial SP, never clobbered)
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
   POSIX initial stack builder
   ---------------------------------------------------------------------- */
/*
 * Build the Linux i386 initial stack layout just below USER_INITIAL_SP:
 *
 *   [esp]          EXIT_STUB_ADDR  (fake return addr — if _start returns, exits cleanly)
 *   [esp+4]        argc            (always 1 — argv[0] = program path)
 *   [esp+8]        argv[0]         (pointer to path string below)
 *   [esp+12]       NULL            (argv terminator)
 *   [esp+16]       NULL            (envp terminator — no environment)
 *   [esp+20]       AT_NULL(0)      (auxv type)
 *   [esp+24]       0               (auxv value)
 *   [esp+28...]    path string
 *
 * The fake return address at [esp] means that a _start() that falls off the
 * end with a plain `ret` will jump to EXIT_STUB_ADDR (mov $1,%eax; int $0x80)
 * and exit cleanly instead of crashing with an invalid EIP.
 *
 * crt0.c reads argc from [esp+4] (not [esp]) to skip the return address.
 *
 * Returns the new user ESP (points at EXIT_STUB_ADDR).
 */
uint32_t build_posix_stack(const char *path) {
    uint8_t *top = (uint8_t *)USER_INITIAL_SP;
    uint8_t *p   = top;

    /* Copy path string just below the stack top */
    size_t len = strlen(path) + 1;
    p -= len;
    for (size_t i = 0; i < len; i++) p[i] = path[i];
    uint32_t argv0_addr = (uint32_t)p;

    /* Align down to 4 bytes */
    p = (uint8_t *)((uint32_t)p & ~3U);

    /* auxv: AT_NULL (type=0, value=0) */
    p -= 4; *(uint32_t *)p = 0;
    p -= 4; *(uint32_t *)p = 0;

    /* envp: NULL terminator (no environment variables) */
    p -= 4; *(uint32_t *)p = 0;

    /* argv: argv[0] pointer, NULL terminator */
    p -= 4; *(uint32_t *)p = 0;
    p -= 4; *(uint32_t *)p = argv0_addr;

    /* argc = 1 */
    p -= 4; *(uint32_t *)p = 1;

    /*
     * Fake return address: if _start() executes a bare `ret` without calling
     * exit(), it pops this address and lands at the exit stub rather than at
     * whatever garbage was in argc (which previously caused EIP=1 crashes).
     */
    p -= 4; *(uint32_t *)p = EXIT_STUB_ADDR;

    return (uint32_t)p;
}

/* -------------------------------------------------------------------------
   Physical-memory POSIX stack builder (used by exec_elf_spawn)
   ---------------------------------------------------------------------- */

/*
 * Identical logic to build_posix_stack() but writes into a physical memory
 * block instead of the live virtual user address space.
 *
 * phys_mem  — start of the 1 MB block (kernel virtual = physical addr).
 *   In the spawned process's page dir, this block is mapped to USER_BASE,
 *   so virtual address = USER_BASE + offset_within_block.
 */
uint32_t build_posix_stack_phys(uint8_t *phys_mem, const char *path) {
    /* Stack top in physical memory: offset of USER_INITIAL_SP within user space */
    uint32_t top_off = USER_INITIAL_SP - USER_BASE;
    uint8_t *p       = phys_mem + top_off;

    /* argv[0] path string */
    size_t len = strlen(path) + 1;
    p -= len;
    for (size_t i = 0; i < len; i++) p[i] = path[i];
    /* Virtual address of the path string as seen by the process */
    uint32_t argv0_vaddr = USER_BASE + (uint32_t)(p - phys_mem);

    /* Align to 4 bytes */
    p = (uint8_t *)((uint32_t)p & ~3U);

    /* auxv: AT_NULL (type=0, value=0) */
    p -= 4; *(uint32_t *)p = 0;
    p -= 4; *(uint32_t *)p = 0;

    /* envp: NULL terminator */
    p -= 4; *(uint32_t *)p = 0;

    /* argv: argv[0] pointer, NULL terminator */
    p -= 4; *(uint32_t *)p = 0;
    p -= 4; *(uint32_t *)p = argv0_vaddr;

    /* argc = 1 */
    p -= 4; *(uint32_t *)p = 1;

    /* Fake return address: lands on exit stub if _start does a bare ret */
    p -= 4; *(uint32_t *)p = EXIT_STUB_ADDR;

    /* Return the VIRTUAL user ESP (process sees USER_BASE-relative addresses) */
    return USER_BASE + (uint32_t)(p - phys_mem);
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

    /* Bounds-check: refuse anything outside user space or with malformed fields. */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t dest_start = phdr[i].p_vaddr + base;
        uint32_t dest_end   = dest_start + phdr[i].p_memsz;
        if (dest_start < USER_BASE || dest_end > USER_STACK) {
            klog_hex("elf", "unsafe load dest", dest_start);
            vga_write_line("ELF: load region outside user space — refused");
            return -1;
        }
        if (phdr[i].p_filesz > phdr[i].p_memsz) {
            vga_write_line("ELF: p_filesz > p_memsz — refused");
            return -1;
        }
        /* p_offset + p_filesz must not overflow */
        if (phdr[i].p_offset + phdr[i].p_filesz < phdr[i].p_offset) {
            vga_write_line("ELF: p_offset overflow — refused");
            return -1;
        }
    }

    /*
     * Reset CR3 to the kernel's global page directory before touching user
     * space.  After multi-process runs (e.g. multi.elf), proc_exit() returns
     * to exec_elf via exit_trampoline without restoring CR3.  CR3 is left
     * pointing at the last dying child's page directory, which maps
     * 0x300000–0x3FFFFF to a different physical frame.  Any write to user
     * space (ELF copy, stack setup) or sbrk mapping would silently target
     * the wrong physical memory, causing page faults on the next run.
     */
    paging_switch_dir(paging_get_page_dir());

    /* Copy segments and zero BSS.
     * Clamp filesz to what was actually read so we never walk off the buffer. */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint8_t *dest = (uint8_t *)(phdr[i].p_vaddr + base);
        uint8_t *src  = (uint8_t *)data + phdr[i].p_offset;
        uint32_t filesz = phdr[i].p_filesz;
        uint32_t memsz  = phdr[i].p_memsz;
        /* Don't read past the ELF_LOAD_BUF we were given */
        if (phdr[i].p_offset < ELF_LOAD_BUF &&
            filesz > ELF_LOAD_BUF - phdr[i].p_offset)
            filesz = ELF_LOAD_BUF - phdr[i].p_offset;
        for (uint32_t j = 0; j < filesz; j++) dest[j] = src[j];
        for (uint32_t j = filesz; j < memsz; j++) dest[j] = 0;
    }

    uint32_t entry = ehdr->e_entry + base;

    /* Validate entry point is within the user code region */
    if (entry < USER_BASE || entry >= USER_STACK) {
        klog_hex("elf", "entry out of range", entry);
        vga_write_line("ELF: entry point outside user space — refused");
        return -1;
    }

    /*
     * Plant stubs above USER_INITIAL_SP so the downward-growing stack cannot
     * overwrite them:
     *   SIG_TRAMPOLINE_ADDR (0x3FFFE8): sigreturn trampoline
     *   EXIT_STUB_ADDR      (0x3FFFF0): exit stub (Linux SYS_EXIT=1)
     */
    /* Sigreturn trampoline: mov $119, %eax; int $0x80; hlt */
    static const uint8_t sigret_stub[] = {
        0xB8, 0x77, 0x00, 0x00, 0x00,  /* mov $119, %eax */
        0xCD, 0x80,                      /* int $0x80      */
        0xF4                             /* hlt            */
    };
    /* Exit stub: mov $1, %eax; xor %ebx,%ebx; int $0x80; hlt */
    static const uint8_t exit_stub[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00,  /* mov $1, %eax   */
        0x31, 0xDB,                      /* xor %ebx,%ebx  */
        0xCD, 0x80,                      /* int $0x80      */
        0xF4                             /* hlt (safety)   */
    };

    uint8_t *sigret_ptr = (uint8_t *)SIG_TRAMPOLINE_ADDR;
    for (uint32_t i = 0; i < sizeof(sigret_stub); i++) sigret_ptr[i] = sigret_stub[i];

    uint8_t *exit_ptr = (uint8_t *)EXIT_STUB_ADDR;
    for (uint32_t i = 0; i < sizeof(exit_stub); i++) exit_ptr[i] = exit_stub[i];

    /* Build POSIX initial stack (argc/argv/envp/auxv). */
    uint32_t user_sp = build_posix_stack("kernel");

    klog_hex("elf", "entry",       entry);
    klog_hex("elf", "user_sp",     user_sp);
    klog_hex("elf", "exit_stub",   EXIT_STUB_ADDR);
    klog_hex("elf", "sigret_stub", SIG_TRAMPOLINE_ADDR);
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

/* -------------------------------------------------------------------------
   exec_elf_spawn — non-blocking ELF launcher for concurrent processes
   ---------------------------------------------------------------------- */

/*
 * Load the ELF binary at `data` (= PROC_SLOT_PHYS(slot)) into slot `slot`
 * and make it PROC_RUNNABLE.  Does NOT block, does NOT call kmalloc_reset().
 *
 * The ELF binary has already been read from disk into the slot's physical
 * memory region.  Segments are rearranged in-place (forward copy, safe for
 * our single-PT_LOAD linker.ld layout where dst_offset ≤ src_offset).
 *
 * After this function returns the scheduler will switch to the new process
 * on the next PIT tick.  When the new process calls SYS_EXIT, proc_exit()
 * either switches to another live process or fires exit_trampoline (which
 * returns control to the blocking exec_elf() call that set kernel_esp).
 */
int exec_elf_spawn(void *data, uint32_t data_len, int slot) {
    serial_write(COM1, "[elf_spawn] entry slot=");
    serial_write_dec(COM1, (uint32_t)slot);
    serial_write(COM1, " data_len=");
    serial_write_hex(COM1, data_len);
    serial_write(COM1, " state=");
    serial_write_dec(COM1, (uint32_t)(slot >= 1 && slot < MAX_PROCS ? proc_table[slot].state : 99));
    serial_write(COM1, "\n");

    if (!data || data_len == 0) { serial_write(COM1, "[elf_spawn] FAIL: null/empty\n"); return -1; }
    if (slot < 1 || slot >= MAX_PROCS) { serial_write(COM1, "[elf_spawn] FAIL: slot OOB\n"); return -1; }
    if (proc_table[slot].state != PROC_DEAD) {
        serial_write(COM1, "[elf_spawn] FAIL: slot not DEAD state=");
        serial_write_dec(COM1, (uint32_t)proc_table[slot].state);
        serial_write(COM1, "\n");
        return -1;
    }

    if (elf_validate(data) != 0) {
        serial_write(COM1, "[elf_spawn] FAIL: invalid ELF magic/header\n");
        return -1;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)data;
    Elf32_Phdr *phdr = (Elf32_Phdr *)((uint8_t *)data + ehdr->e_phoff);

    /* Compute load bias so first PT_LOAD lands at USER_BASE */
    uint32_t base = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) { base = USER_BASE - phdr[i].p_vaddr; break; }
    }

    /* Bounds-check all segments before touching memory */
    uint32_t entry = ehdr->e_entry + base;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t ds = phdr[i].p_vaddr + base;
        uint32_t de = ds + phdr[i].p_memsz;
        if (ds < USER_BASE || de > USER_STACK) { klog("elf_spawn", "segment OOB"); return -1; }
        if (phdr[i].p_filesz > phdr[i].p_memsz) return -1;
        if (phdr[i].p_offset + phdr[i].p_filesz < phdr[i].p_offset) return -1;
    }
    if (entry < USER_BASE || entry >= USER_STACK) { klog("elf_spawn", "entry OOB"); return -1; }

    /*
     * Save segment info before any in-place moves that could overwrite the
     * ELF header / program headers at the start of the buffer.
     */
    struct { uint32_t src_off, dst_off, filesz, memsz; } segs[4];
    int nseg = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum && nseg < 4; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t dst_off = (phdr[i].p_vaddr + base) - USER_BASE;
        uint32_t filesz  = phdr[i].p_filesz;
        if (phdr[i].p_offset < data_len && filesz > data_len - phdr[i].p_offset)
            filesz = data_len - phdr[i].p_offset;
        segs[nseg].src_off = phdr[i].p_offset;
        segs[nseg].dst_off = dst_off;
        segs[nseg].filesz  = filesz;
        segs[nseg].memsz   = phdr[i].p_memsz;
        nseg++;
    }

    uint8_t *pm = (uint8_t *)data;  /* physical base pointer */

    /*
     * Rearrange segments in-place.
     * For our single-PT_LOAD layout (p_vaddr=0x300000=USER_BASE),
     * dst_off = 0 and src_off = p_offset (small, e.g. 0 or 0x1000).
     * When src_off > dst_off a forward copy is safe (dst ≤ src).
     * Then zero BSS (p_memsz − p_filesz bytes past the copied data).
     */
    for (int s = 0; s < nseg; s++) {
        uint8_t *dst   = pm + segs[s].dst_off;
        uint8_t *src   = pm + segs[s].src_off;
        uint32_t fsz   = segs[s].filesz;
        uint32_t msz   = segs[s].memsz;

        if (dst != src) {
            /* Forward copy: safe when dst ≤ src */
            for (uint32_t j = 0; j < fsz; j++) dst[j] = src[j];
        }
        /* Zero BSS */
        for (uint32_t j = fsz; j < msz; j++) dst[j] = 0;
    }

    /* Plant exit stub and signal trampoline in physical memory */
    static const uint8_t sigret_stub[] = {
        0xB8, 0x77, 0x00, 0x00, 0x00,  /* mov $119, %eax */
        0xCD, 0x80,                      /* int $0x80      */
        0xF4                             /* hlt            */
    };
    static const uint8_t exit_stub_bytes[] = {
        0xB8, 0x01, 0x00, 0x00, 0x00,  /* mov $1, %eax   */
        0x31, 0xDB,                      /* xor %ebx,%ebx  */
        0xCD, 0x80,                      /* int $0x80      */
        0xF4                             /* hlt            */
    };
    uint8_t *sp = pm + (SIG_TRAMPOLINE_ADDR - USER_BASE);
    for (uint32_t i = 0; i < sizeof(sigret_stub); i++) sp[i] = sigret_stub[i];
    uint8_t *ep = pm + (EXIT_STUB_ADDR - USER_BASE);
    for (uint32_t i = 0; i < sizeof(exit_stub_bytes); i++) ep[i] = exit_stub_bytes[i];

    /* Build POSIX stack in physical memory; returns virtual user_sp */
    uint32_t user_sp = build_posix_stack_phys(pm, "kernel");

    klog_hex("elf_spawn", "slot",     (uint32_t)slot);
    klog_hex("elf_spawn", "entry",    entry);
    klog_hex("elf_spawn", "user_sp",  user_sp);
    klog_hex("elf_spawn", "phys_base",(uint32_t)pm);

    return proc_spawn_user(entry, user_sp, slot, (uint32_t)pm);
}
