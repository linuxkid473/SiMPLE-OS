/*
 * process.c — process table, PID allocation, cooperative scheduler,
 *             context switching, and fork().
 *
 * Memory layout for user process images:
 *   slot 0 (initial): virtual 0x300000 → physical 0x300000 (kernel page_dir)
 *   slot 1 (child 1): virtual 0x300000 → physical 0x500000
 *   slot 2 (child 2): virtual 0x300000 → physical 0x600000
 *   slot 3 (child 3): virtual 0x300000 → physical 0x700000
 *
 * Context switch mechanism:
 *   When SYS_YIELD or SYS_EXIT is called, we are inside isr_handler() which
 *   received a `registers_t *regs` pointing to the current kernel ISR stack.
 *   We save that struct into the current process's saved_regs, then overwrite
 *   *regs with the next process's saved_regs.  The isr_syscall epilogue then
 *   pops the modified registers and irets into the next process automatically.
 *   We also update CR3 (address space) and TSS.esp0 (ISR kernel stack) before
 *   returning so the next ring3→ring0 transition uses the right stack.
 */

#include "elf.h"      /* USER_BASE, USER_STACK, exit_trampoline */
#include "fd.h"
#include "gdt.h"      /* tss_set_esp0, SEG_KCODE */
#include "klog.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "types.h"

process_t proc_table[MAX_PROCS];
int       current_proc = -1;

/*
 * Per-process page directories, page tables, and ISR kernel stacks.
 * All must be page-aligned; static in BSS guarantees zero-init.
 * Total: 4*(4+4+4) = 48 KB — well within kernel BSS.
 */
static uint32_t proc_pdirs  [MAX_PROCS][1024] __attribute__((aligned(4096)));
static uint32_t proc_ptabs  [MAX_PROCS][1024] __attribute__((aligned(4096)));
static uint8_t  proc_kstacks[MAX_PROCS][4096] __attribute__((aligned(16)));

void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_table[i].pid      = -1;
        proc_table[i].state    = PROC_DEAD;
        proc_table[i].page_dir = (uint32_t *)0;
    }
    current_proc = -1;
}

/*
 * Register the initial exec'd process in slot 0.
 * Called by exec_elf() before launch_ring3().
 */
void proc_register_initial(uint32_t *page_dir, fd_table_t *fdt) {
    proc_table[0].pid      = 1;
    proc_table[0].state    = PROC_RUNNING;
    proc_table[0].page_dir = page_dir;
    proc_table[0].exit_code = 0;
    if (fdt)
        proc_table[0].fd_table = *fdt;
    else
        fd_table_init(&proc_table[0].fd_table);
    current_proc = 0;
    /* Switch ISR stack to proc_kstacks[0] for this process */
    tss_set_esp0((uint32_t)(proc_kstacks[0] + 4096));
}

/* Allocate a free child slot (slots 1–MAX_PROCS-1 only).
 * Returns slot index or -1 if full. */
static int alloc_child_slot(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) {
            proc_table[i].pid       = i + 1;
            proc_table[i].state     = PROC_RUNNABLE;
            proc_table[i].exit_code = 0;
            fd_table_init(&proc_table[i].fd_table);
            return i;
        }
    }
    return -1;
}

/* Round-robin: find next RUNNABLE process starting after `from`. */
static int sched_next_after(int from) {
    for (int i = 1; i <= MAX_PROCS; i++) {
        int slot = (from + i) % MAX_PROCS;
        if (proc_table[slot].state == PROC_RUNNABLE)
            return slot;
    }
    return -1;
}

/* Perform the actual context switch: update CR3, TSS.esp0, overwrite *regs. */
static void do_switch(int next_slot, registers_t *regs) {
    proc_table[next_slot].state = PROC_RUNNING;
    current_proc = next_slot;
    paging_switch_dir(proc_table[next_slot].page_dir);
    tss_set_esp0((uint32_t)(proc_kstacks[next_slot] + 4096));
    *regs = proc_table[next_slot].saved_regs;
}

/* Globals from elf.c for the exit_trampoline return path. */
extern int  process_exited;

void proc_yield(registers_t *regs) {
    if (current_proc < 0) return;

    int next = sched_next_after(current_proc);
    if (next < 0) return;  /* only one process — keep running */

    proc_table[current_proc].saved_regs = *regs;
    proc_table[current_proc].state      = PROC_RUNNABLE;

    serial_write(COM1, "[proc] yield: ");
    serial_write_dec(COM1, (uint32_t)current_proc);
    serial_write(COM1, " -> ");
    serial_write_dec(COM1, (uint32_t)next);
    serial_write(COM1, "\n");

    do_switch(next, regs);
}

void proc_exit(registers_t *regs, int code) {
    int dying = current_proc;

    if (dying >= 0) {
        proc_table[dying].state     = PROC_DEAD;
        proc_table[dying].exit_code = code;
        serial_write(COM1, "[proc] exit pid=");
        serial_write_dec(COM1, (uint32_t)proc_table[dying].pid);
        serial_write(COM1, "\n");
    }

    int next = sched_next_after(dying >= 0 ? dying : 0);
    if (next >= 0) {
        current_proc = -1;  /* clear before do_switch sets it */
        do_switch(next, regs);
    } else {
        /* No more runnable processes — return to exec_elf via exit_trampoline */
        current_proc  = -1;
        process_exited = 1;
        regs->eip    = (uint32_t)exit_trampoline;
        regs->cs     = SEG_KCODE;
        regs->eflags = 0x02;
    }
}

int proc_fork(registers_t *regs) {
    if (current_proc < 0) return -1;

    int child_slot = alloc_child_slot();
    if (child_slot < 0) {
        klog("proc", "fork: no free slots");
        return -1;
    }

    process_t *parent = &proc_table[current_proc];
    process_t *child  = &proc_table[child_slot];

    /*
     * Physical base for child's user image.
     * child_slot is always >=1; slot 1→0x500000, slot 2→0x600000, slot 3→0x700000.
     */
    uint32_t child_phys = PROC_POOL_BASE + (uint32_t)(child_slot - 1) * PROC_USER_SIZE;

    /* 1. Physically copy the entire user region (code + data + stack). */
    uint8_t *src = (uint8_t *)USER_BASE;
    uint8_t *dst = (uint8_t *)child_phys;
    uint32_t region = USER_STACK - USER_BASE;  /* 1 MB */
    for (uint32_t i = 0; i < region; i++)
        dst[i] = src[i];

    /* 2. Build child's page directory: kernel mappings shared, user remapped. */
    paging_clone(proc_pdirs[child_slot], proc_ptabs[child_slot], child_phys);
    child->page_dir = proc_pdirs[child_slot];

    /* 3. Clone fd table from parent. */
    child->fd_table = parent->fd_table;

    /* 4. Clone CPU state; child returns 0 from fork. */
    child->saved_regs     = *regs;
    child->saved_regs.eax = 0;

    child->state = PROC_RUNNABLE;

    klog_dec("proc", "fork: child pid", (uint32_t)child->pid);
    klog_hex("proc", "fork: child phys", child_phys);

    /* Parent gets child pid as fork() return value. */
    return child->pid;
}
