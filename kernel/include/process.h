#ifndef SIMPLE_PROCESS_H
#define SIMPLE_PROCESS_H

#include "fd.h"
#include "registers.h"
#include "types.h"

#define MAX_PROCS      4
#define PROC_USER_SIZE 0x100000U   /* 1 MB per child user image */
/*
 * Physical base for child process user images.
 * Slot n (1-based child slot) uses PROC_POOL_BASE + (n-1)*PROC_USER_SIZE.
 *   slot 1 → 0x500000, slot 2 → 0x600000, slot 3 → 0x700000
 * These are within PDE[1] (0x400000-0x7FFFFF), supervisor-accessible
 * from the kernel and remapped as user pages in each child's page directory.
 */
#define PROC_POOL_BASE 0x500000U

/*
 * Number of timer ticks a process runs before the scheduler preempts it.
 * At PIT_HZ=100 Hz, PROC_TIMESLICE=10 gives 100 ms time slices.
 */
#define PROC_TIMESLICE 10U

typedef enum {
    PROC_DEAD     = 0,   /* slot is free, may be reused           */
    PROC_RUNNING  = 1,   /* currently executing on CPU            */
    PROC_RUNNABLE = 2,   /* ready to run, waiting for CPU         */
    PROC_ZOMBIE   = 3,   /* exited, waiting to be reaped (future) */
    PROC_BLOCKED  = 4,   /* sleeping / waiting for I/O (future)   */
} proc_state_t;

typedef struct {
    int          pid;
    int          parent_pid;       /* pid of creating process, or -1 if none        */
    proc_state_t state;
    registers_t  saved_regs;       /* saved user-side CPU state (iret frame + gprs) */
    uint32_t    *page_dir;         /* pointer to this process's 4KB page directory  */
    fd_table_t   fd_table;
    int          exit_code;
    uint32_t     ticks_remaining;  /* preemption countdown; reset on each switch-in */
} process_t;

extern process_t proc_table[MAX_PROCS];
extern int       current_proc;         /* index into proc_table[], -1 = no process */

void proc_init(void);

/* Register the initial (exec'd) process in slot 0. */
void proc_register_initial(uint32_t *page_dir, fd_table_t *fdt);

/* Cooperative yield: save current state, schedule next runnable process. */
void proc_yield(registers_t *regs);

/* Exit: mark dead, schedule next or fall back to exit_trampoline. */
void proc_exit(registers_t *regs, int code);

/* Fork: duplicate current process. Returns child pid to parent (in regs->eax),
 * child will see eax=0 when it first runs. Returns -1 on failure. */
int proc_fork(registers_t *regs);

/* Wait: block until any child exits, reap it, return its exit code.
 * Returns -1 immediately if no children exist. */
int proc_wait(registers_t *regs);

/*
 * Called from the PIT timer IRQ handler (pit_timer_tick).
 * Decrements the current process's time slice and preempts if expired and
 * another RUNNABLE process exists.  Only preempts ring3 code (CS.RPL=3).
 */
void proc_timer_tick(registers_t *regs);

#endif
