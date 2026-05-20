/*
 * process.c — process table, PID allocation, preemptive + cooperative scheduler,
 *             context switching, and fork().
 *
 * Memory layout for user process images:
 *   slot 0 (initial): virtual 0x300000 → physical 0x300000 (kernel page_dir)
 *   slot 1 (child 1): virtual 0x300000 → physical 0x500000
 *   slot 2 (child 2): virtual 0x300000 → physical 0x600000
 *   slot 3 (child 3): virtual 0x300000 → physical 0x700000
 *
 * Context switch mechanism (same for cooperative yield AND timer preemption):
 *   The caller holds a `registers_t *regs` pointing to the full saved CPU
 *   state on the current process's kernel ISR stack.  We copy that struct
 *   into current_proc.saved_regs, then overwrite *regs with next_proc.saved_regs.
 *   The ISR epilogue (pop segs, popa, add $8, iret) then restores the new
 *   process's state and irets into it.  CR3 and TSS.esp0 are updated before
 *   returning so the next ring3→ring0 event uses the correct address space
 *   and kernel stack.
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
    proc_table[0].pid             = 1;
    proc_table[0].state           = PROC_RUNNING;
    proc_table[0].page_dir        = page_dir;
    proc_table[0].exit_code       = 0;
    proc_table[0].ticks_remaining = PROC_TIMESLICE;
    if (fdt)
        proc_table[0].fd_table = *fdt;
    else
        fd_table_init(&proc_table[0].fd_table);
    current_proc = 0;
    /* Switch ISR stack to proc_kstacks[0] for this process */
    tss_set_esp0((uint32_t)(proc_kstacks[0] + 4096));

    serial_write(COM1, "[proc] initial pid=1 kstack=");
    serial_write_hex(COM1, (uint32_t)(proc_kstacks[0] + 4096));
    serial_write(COM1, "\n");
}

/* Allocate a free child slot (slots 1–MAX_PROCS-1 only).
 * Returns slot index or -1 if full. */
static int alloc_child_slot(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) {
            proc_table[i].pid             = i + 1;
            proc_table[i].state           = PROC_RUNNABLE;
            proc_table[i].exit_code       = 0;
            proc_table[i].ticks_remaining = PROC_TIMESLICE;
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

/* Count processes that are alive (RUNNING or RUNNABLE). */
static int proc_alive_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_RUNNING ||
            proc_table[i].state == PROC_RUNNABLE)
            n++;
    }
    return n;
}

/* Perform the actual context switch: update CR3, TSS.esp0, overwrite *regs. */
static void do_switch(int next_slot, registers_t *regs) {
    /* Give the incoming process a fresh time slice */
    proc_table[next_slot].ticks_remaining = PROC_TIMESLICE;
    proc_table[next_slot].state           = PROC_RUNNING;
    current_proc = next_slot;

    /* Switch address space */
    paging_switch_dir(proc_table[next_slot].page_dir);

    /* Update TSS.esp0 so the next ring3→ring0 transition (syscall or IRQ)
     * saves state on this process's dedicated kernel stack */
    tss_set_esp0((uint32_t)(proc_kstacks[next_slot] + 4096));

    /* Overwrite the iret frame in-place so the ISR epilogue jumps into
     * the new process instead of the old one */
    *regs = proc_table[next_slot].saved_regs;

    serial_write(COM1, "[sched] switch→pid=");
    serial_write_dec(COM1, (uint32_t)proc_table[next_slot].pid);
    serial_write(COM1, " eip=");
    serial_write_hex(COM1, proc_table[next_slot].saved_regs.eip);
    serial_write(COM1, " cr3=");
    serial_write_hex(COM1, (uint32_t)proc_table[next_slot].page_dir);
    serial_write(COM1, " kstk=");
    serial_write_hex(COM1, (uint32_t)(proc_kstacks[next_slot] + 4096));
    serial_write(COM1, "\n");
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
    uint8_t *src    = (uint8_t *)USER_BASE;
    uint8_t *dst    = (uint8_t *)child_phys;
    uint32_t region = USER_STACK - USER_BASE;  /* 1 MB */
    for (uint32_t i = 0; i < region; i++)
        dst[i] = src[i];

    /* 2. Build child's page directory: kernel mappings shared, user remapped. */
    paging_clone(proc_pdirs[child_slot], proc_ptabs[child_slot], child_phys);
    child->page_dir = proc_pdirs[child_slot];

    /* 3. Clone fd table from parent. */
    child->fd_table = parent->fd_table;

    /* 4. Clone CPU state; child returns 0 from fork.
     *    The child's saved EFLAGS has IF=1 (trap gate doesn't clear IF),
     *    so the child runs with interrupts enabled from the first tick. */
    child->saved_regs     = *regs;
    child->saved_regs.eax = 0;

    child->state = PROC_RUNNABLE;

    serial_write(COM1, "[proc] fork: child pid=");
    serial_write_dec(COM1, (uint32_t)child->pid);
    serial_write(COM1, " phys=");
    serial_write_hex(COM1, child_phys);
    serial_write(COM1, " eip=");
    serial_write_hex(COM1, child->saved_regs.eip);
    serial_write(COM1, "\n");

    /* Parent gets child pid as fork() return value. */
    return child->pid;
}

/*
 * proc_timer_tick — called from the PIT IRQ0 handler once per tick.
 *
 * Only preempts ring3 code (CS.RPL = 3).  Kernel-mode execution (ring0 CS)
 * is never preempted here: the CS check guards against interrupting a
 * syscall handler or other kernel code that's not safe to switch away from.
 *
 * If the current process's time slice has expired and another RUNNABLE
 * process exists, saves current state and switches to the next process.
 * The switch modifies *regs in-place; the IRQ0 iret frame then carries
 * the new process into execution.
 */
void proc_timer_tick(registers_t *regs) {
    /* Nothing running or interrupted from kernel mode: nothing to preempt */
    if (current_proc < 0)           return;
    if ((regs->cs & 3) != 3)        return;
    if (proc_alive_count() < 2)     return;  /* alone — no point switching */

    /* Decrement time slice */
    if (proc_table[current_proc].ticks_remaining > 0)
        proc_table[current_proc].ticks_remaining--;

    if (proc_table[current_proc].ticks_remaining > 0)
        return;  /* still has time left */

    /* Slice expired — find next runnable process */
    int next = sched_next_after(current_proc);
    if (next < 0) {
        /* No other runnable process; reset slice and keep current running */
        proc_table[current_proc].ticks_remaining = PROC_TIMESLICE;
        return;
    }

    /* Save current process CPU state from the IRQ frame */
    proc_table[current_proc].saved_regs = *regs;
    proc_table[current_proc].state      = PROC_RUNNABLE;

    serial_write(COM1, "[sched] preempt pid=");
    serial_write_dec(COM1, (uint32_t)proc_table[current_proc].pid);
    serial_write(COM1, " eip=");
    serial_write_hex(COM1, regs->eip);
    serial_write(COM1, " esp=");
    serial_write_hex(COM1, regs->useresp);
    serial_write(COM1, "\n");

    do_switch(next, regs);
}
