#ifndef SIMPLE_PROCESS_H
#define SIMPLE_PROCESS_H
#include "fd.h"
#include "registers.h"
#include "signal.h"
#include "types.h"

#define MAX_PROCS       8
#define PROC_TIMESLICE  10U

/* Wait status encoding (POSIX) */
#define W_EXITED(code)    (((code) & 0xFF) << 8)
#define W_SIGNALED(sig)   ((sig) & 0x7F)
#define W_STOPPED(sig)    (((sig) & 0xFF) << 8 | 0x7F)
#define WIFEXITED(s)      (((s) & 0x7F) == 0)
#define WEXITSTATUS(s)    (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s)    (((s) & 0x7F) != 0 && ((s) & 0x7F) != 0x7F)
#define WTERMSIG(s)       ((s) & 0x7F)
#define WIFSTOPPED(s)     (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s)       (((s) >> 8) & 0xFF)

/* waitpid options */
#define WNOHANG    1
#define WUNTRACED  2

typedef enum {
    PROC_DEAD     = 0,
    PROC_RUNNING  = 1,
    PROC_RUNNABLE = 2,
    PROC_ZOMBIE   = 3,
    PROC_BLOCKED  = 4,   /* waiting in waitpid */
    PROC_SLEEPING = 5,
    PROC_STOPPED  = 6,   /* SIGSTOP/SIGTSTP */
} proc_state_t;

#define PROC_NAME_LEN 32
#define CWD_MAX       256

typedef struct {
    /* Identity */
    pid_t         pid;
    pid_t         parent_pid;
    pgid_t        pgid;
    sid_t         sid;
    uid_t         uid, euid;
    gid_t         gid, egid;
    uint32_t      umask;

    /* State */
    proc_state_t  state;
    int           exit_status;   /* POSIX encoded (W_EXITED etc.) */

    /* CPU state */
    registers_t   saved_regs;
    uint32_t     *page_dir;

    /* Scheduling */
    uint32_t      ticks_remaining;
    uint32_t      sleep_until;
    uint32_t      brk;

    /* Blocking — which pipe index this process is waiting on (-1 = none) */
    int           pipe_wait_idx;

    /* File descriptors */
    fd_table_t    fd_table;

    /* Signals */
    uint32_t      sig_pending;
    uint32_t      sig_mask;
    struct sigaction sig_actions[NSIG];

    /* Filesystem */
    char          cwd[CWD_MAX];

    /* Name */
    char          name[PROC_NAME_LEN];
} process_t;

extern process_t proc_table[MAX_PROCS];
extern int       current_proc;

#define CURRENT  (&proc_table[current_proc])

/* Init */
void proc_init(void);
void proc_register_initial(uint32_t *page_dir, fd_table_t *fdt);

/* Scheduling */
void proc_yield(registers_t *regs);
void proc_timer_tick(registers_t *regs);

/* Lifecycle */
void  proc_exit(registers_t *regs, int status);   /* status is POSIX-encoded */
int   proc_fork(registers_t *regs);
pid_t proc_waitpid(pid_t pid, int *status, int options, registers_t *regs);
void  proc_sleep(registers_t *regs, uint32_t ticks);

/* Legacy wait wrapper */
int   proc_wait(registers_t *regs);

/* Signals */
int  proc_send_signal(pid_t pid, int sig);
void proc_deliver_signals(registers_t *regs);
void proc_send_signal_group(pgid_t pgid, int sig);

/* Helpers */
int  proc_find_by_pid(pid_t pid);

/* Instrumentation */
void proc_dump_table(const char *tag);

/* Pipe blocking: block current process waiting for pipe idx; wake waiters */
void proc_block_on_pipe(int pipe_idx, registers_t *regs);
void proc_wake_pipe_waiters(int pipe_idx);

/*
 * proc_block_on_kbd — put the current process to sleep waiting for a
 * keyboard scancode.  Saves the register frame with EIP-=2 so that on
 * wake-up the process re-executes "int $0x80" and retries the read syscall.
 * Switches to the next runnable process immediately.
 *
 * proc_wake_kbd_waiters — called by keyboard_irq_handler after putting a
 * byte in the scancode ring buffer; sets all kbd-blocked processes RUNNABLE.
 */
void proc_block_on_kbd(registers_t *regs);
void proc_wake_kbd_waiters(void);

/*
 * Concurrent ELF spawning — non-blocking alternative to exec_elf().
 *
 * proc_find_spawn_slot() — return the first free slot index (1–7), or -1.
 *
 * proc_spawn_user() — configure process slot `slot` with the given entry
 *   point, user stack pointer, and physical memory base for 0x300000-0x3FFFFF,
 *   then mark it PROC_RUNNABLE.  The preemptive timer (IRQ0) will schedule
 *   it on the next tick.  Does NOT reset the kmalloc heap or the physical
 *   page pool — safe to call while another process is running.
 *   Returns `slot` on success, -1 on failure.
 */
int proc_find_spawn_slot(void);
int proc_spawn_user(uint32_t entry, uint32_t user_sp,
                    int slot, uint32_t phys_user_base);

/* Used by elf.c */
extern uint32_t kernel_esp;
extern int      process_exited;
extern uint32_t saved_ebp, saved_ebx, saved_esi, saved_edi;
void exit_trampoline(void);

#endif
