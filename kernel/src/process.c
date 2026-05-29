/*
 * process.c — process table, scheduler, context switching, fork, signals.
 */

#include "elf.h"      /* USER_BASE, USER_STACK, exit_trampoline */
#include "fd.h"
#include "gdt.h"      /* tss_set_esp0, SEG_KCODE */
#include "klog.h"
#include "kmalloc.h"
#include "paging.h"
#include "pit.h"
#include "posix_errno.h"
#include "process.h"
#include "serial.h"
#include "signal.h"
#include "types.h"
#include "wm.h"

process_t proc_table[MAX_PROCS];
int       current_proc = -1;

/* Saved ISR frame for the ring-0 shell (hlt loop).
 * Captured by proc_timer_tick before the first ever switch to ring-3.
 * Restored by proc_exit when the last ring-3 process exits and kernel_esp==0
 * (i.e., exec_elf_spawn was used, not the blocking exec_elf). */
static registers_t saved_ring0_regs;
static int         ring0_has_saved_context = 0;

/* Target ESP for the ring-0 resume trampoline (points at the 3-item IRET
 * frame that was rebuilt on the original ring-0 stack).
 * Must be non-static so the naked inline asm in ring0_resume_trampoline
 * can reference it by name as a global symbol. */
uint32_t ring0_resume_target_esp = 0;

/* Per-process page directories, page tables, and ISR kernel stacks. */
static uint32_t proc_pdirs  [MAX_PROCS][1024] __attribute__((aligned(4096)));
static uint32_t proc_ptabs  [MAX_PROCS][1024] __attribute__((aligned(4096)));
static uint32_t proc_ptabs1 [MAX_PROCS][1024] __attribute__((aligned(4096)));
static uint8_t  proc_kstacks[MAX_PROCS][4096] __attribute__((aligned(16)));

void proc_init(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_table[i].pid        = -1;
        proc_table[i].state      = PROC_DEAD;
        proc_table[i].page_dir   = (uint32_t *)0;
        proc_table[i].sig_pending    = 0;
        proc_table[i].sig_mask       = 0;
        proc_table[i].pipe_wait_idx  = -1;
        for (int s = 0; s < NSIG; s++) {
            proc_table[i].sig_actions[s].sa_handler = SIG_DFL;
            proc_table[i].sig_actions[s].sa_flags   = 0;
            proc_table[i].sig_actions[s].sa_mask     = 0;
        }
    }
    current_proc = -1;
}

void proc_register_initial(uint32_t *page_dir, fd_table_t *fdt) {
    /* Reset any leftover zombie child slots from a previous run. */
    for (int i = 1; i < MAX_PROCS; i++)
        proc_table[i].state = PROC_DEAD;

    /* Clean up any stale USER windows from a previous ELF session. */
    wm_cleanup_all_user_windows();

    kmalloc_reset();

    /*
     * Reset the physical page pool and clear stale heap PTEs.
     *
     * Without this, two things go wrong on the second ELF run:
     *   - paging_alloc_phys_page() returns 0 immediately because the bump
     *     pointer was exhausted by fork() calls in the previous run.
     *   - page_tab1 still has PTE_PRESENT entries from the previous run's
     *     heap growth, so paging_page_mapped() reports pages as already
     *     mapped and sbrk reuses stale physical memory.
     */
    paging_reset_phys_heap();

    proc_table[0].pid             = 1;
    proc_table[0].parent_pid      = -1;
    proc_table[0].pgid            = 1;
    proc_table[0].sid             = 1;
    proc_table[0].uid             = 0;
    proc_table[0].euid            = 0;
    proc_table[0].gid             = 0;
    proc_table[0].egid            = 0;
    proc_table[0].umask           = 022;
    proc_table[0].state           = PROC_RUNNING;
    proc_table[0].page_dir        = page_dir;
    proc_table[0].exit_status     = 0;
    proc_table[0].ticks_remaining = PROC_TIMESLICE;
    proc_table[0].brk             = 0x400000U;
    proc_table[0].sig_pending     = 0;
    proc_table[0].sig_mask        = 0;
    proc_table[0].cwd[0]          = '/';
    proc_table[0].cwd[1]          = '\0';
    for (int s = 0; s < NSIG; s++) {
        proc_table[0].sig_actions[s].sa_handler = SIG_DFL;
        proc_table[0].sig_actions[s].sa_flags   = 0;
        proc_table[0].sig_actions[s].sa_mask     = 0;
    }
    if (fdt)
        proc_table[0].fd_table = *fdt;
    else
        fd_table_init(&proc_table[0].fd_table);
    current_proc = 0;
    paging_switch_dir(page_dir);
    tss_set_esp0((uint32_t)(proc_kstacks[0] + 4096));

    serial_write(COM1, "[proc] initial pid=1\n");
}

static int alloc_child_slot(void) {
    for (int i = 1; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) {
            proc_table[i].pid             = i + 1;
            proc_table[i].parent_pid      = (current_proc >= 0) ? proc_table[current_proc].pid : -1;
            proc_table[i].pgid            = (current_proc >= 0) ? proc_table[current_proc].pgid : i + 1;
            proc_table[i].sid             = (current_proc >= 0) ? proc_table[current_proc].sid : i + 1;
            proc_table[i].uid             = (current_proc >= 0) ? proc_table[current_proc].uid : 0;
            proc_table[i].euid            = (current_proc >= 0) ? proc_table[current_proc].euid : 0;
            proc_table[i].gid             = (current_proc >= 0) ? proc_table[current_proc].gid : 0;
            proc_table[i].egid            = (current_proc >= 0) ? proc_table[current_proc].egid : 0;
            proc_table[i].umask           = (current_proc >= 0) ? proc_table[current_proc].umask : 022;
            proc_table[i].state           = PROC_RUNNABLE;
            proc_table[i].exit_status     = 0;
            proc_table[i].ticks_remaining = PROC_TIMESLICE;
            proc_table[i].brk             = 0x400000U;
            proc_table[i].sig_pending     = 0;
            proc_table[i].pipe_wait_idx   = -1;
            proc_table[i].sig_mask        = (current_proc >= 0) ? proc_table[current_proc].sig_mask : 0;
            for (int s = 0; s < NSIG; s++) {
                if (current_proc >= 0)
                    proc_table[i].sig_actions[s] = proc_table[current_proc].sig_actions[s];
                else {
                    proc_table[i].sig_actions[s].sa_handler = SIG_DFL;
                    proc_table[i].sig_actions[s].sa_flags   = 0;
                    proc_table[i].sig_actions[s].sa_mask     = 0;
                }
            }
            if (current_proc >= 0) {
                int j = 0;
                while (proc_table[current_proc].cwd[j] && j < CWD_MAX - 1) {
                    proc_table[i].cwd[j] = proc_table[current_proc].cwd[j];
                    j++;
                }
                proc_table[i].cwd[j] = '\0';
            } else {
                proc_table[i].cwd[0] = '/';
                proc_table[i].cwd[1] = '\0';
            }
            fd_table_init(&proc_table[i].fd_table);
            return i;
        }
    }
    return -1;
}

static int sched_next_after(int from) {
    for (int i = 1; i <= MAX_PROCS; i++) {
        int slot = (from + i) % MAX_PROCS;
        if (proc_table[slot].state == PROC_RUNNABLE)
            return slot;
    }
    return -1;
}

/* ------------------------------------------------------------------ *
 * INSTRUMENTATION — dumps full process table + WM window table to    *
 * COM1 serial.  Call from SYS_WM_CREATE, proc_exit, etc. for Phase   *
 * 1/2 evidence capture.                                               *
 * ------------------------------------------------------------------ */
static const char *state_names[] = {
    "DEAD", "RUNNING", "RUNNABLE", "ZOMBIE", "BLOCKED", "SLEEPING"
};

void proc_dump_table(const char *tag) {
    serial_write(COM1, "[PTBL] --- ");
    serial_write(COM1, tag);
    serial_write(COM1, " current_proc=");
    if (current_proc < 0)
        serial_write(COM1, "ring0");
    else
        serial_write_dec(COM1, (uint32_t)current_proc);
    serial_write(COM1, " ring0_saved=");
    serial_write_dec(COM1, (uint32_t)ring0_has_saved_context);
    serial_write(COM1, "\n");
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) continue;
        serial_write(COM1, "[PTBL]   slot=");
        serial_write_dec(COM1, (uint32_t)i);
        serial_write(COM1, " pid=");
        serial_write_dec(COM1, (uint32_t)proc_table[i].pid);
        serial_write(COM1, " state=");
        uint32_t s = (uint32_t)proc_table[i].state;
        serial_write(COM1, s < 6 ? state_names[s] : "?");
        serial_write(COM1, "\n");
    }
}

static void do_switch(int next_slot, registers_t *regs) {
    proc_table[next_slot].ticks_remaining = PROC_TIMESLICE;
    proc_table[next_slot].state           = PROC_RUNNING;
    current_proc = next_slot;

    paging_switch_dir(proc_table[next_slot].page_dir);
    tss_set_esp0((uint32_t)(proc_kstacks[next_slot] + 4096));

    *regs = proc_table[next_slot].saved_regs;

    serial_write(COM1, "[sched] switch pid=");
    serial_write_dec(COM1, (uint32_t)proc_table[next_slot].pid);
    serial_write(COM1, "\n");
}

extern int  process_exited;

int proc_find_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_PROCS; i++)
        if (proc_table[i].state != PROC_DEAD && proc_table[i].pid == pid)
            return i;
    return -1;
}

void proc_yield(registers_t *regs) {
    if (current_proc < 0) return;

    int next = sched_next_after(current_proc);
    if (next < 0) return;

    proc_table[current_proc].saved_regs = *regs;
    proc_table[current_proc].state      = PROC_RUNNABLE;

    do_switch(next, regs);
}

/*
 * After restoring ring-0, IRET back to the original ring-0 stack position.
 * We switch ESP to the rebuilt IRET frame on the ring-0 stack and iret there,
 * avoiding the use of the syscall-handler stack (proc_kstacks[0]) entirely.
 */
__attribute__((naked, noinline)) static void ring0_resume_trampoline(void) {
    __asm__ volatile(
        "cli\n\t"
        "movl ring0_resume_target_esp, %%esp\n\t"
        "iret\n\t"
        : : : "memory"
    );
}

void proc_exit(registers_t *regs, int status) {
    int dying = current_proc;

    if (dying >= 0) {
        /* Close all fds */
        for (int fd = 0; fd < FD_MAX; fd++)
            if (proc_table[dying].fd_table.fds[fd].type != FD_NONE)
                fd_close(&proc_table[dying].fd_table, fd);

        /* Release any USER windows owned by this process */
        wm_cleanup_for_slot(dying);

        proc_table[dying].exit_status = status;

        /* Send SIGCHLD to parent; if parent is blocked in wait, reap and wake */
        pid_t ppid = proc_table[dying].parent_pid;
        if (ppid < 0) {
            /* Orphan (no parent) — auto-reap immediately so slot can be reused */
            proc_table[dying].state = PROC_DEAD;
        } else {
            proc_table[dying].state = PROC_ZOMBIE;
            for (int i = 0; i < MAX_PROCS; i++) {
                if (proc_table[i].pid == ppid) {
                    /* Send SIGCHLD */
                    proc_table[i].sig_pending |= (1U << SIGCHLD);

                    if (proc_table[i].state == PROC_BLOCKED) {
                        /* Leave dying as ZOMBIE — parent re-executes waitpid()
                         * via eip-2 and reaps it there (reads exit_status too). */
                        proc_table[i].state = PROC_RUNNABLE;
                        serial_write(COM1, "[PROC] pid=");
                        serial_write_dec(COM1, (uint32_t)proc_table[dying].pid);
                        serial_write(COM1, " state=ZOMBIE woke parent pid=");
                        serial_write_dec(COM1, (uint32_t)ppid);
                        serial_write(COM1, "\n");
                    }
                    break;
                }
            }
        }

        serial_write(COM1, "[PROC] pid=");
        serial_write_dec(COM1, (uint32_t)proc_table[dying].pid);
        serial_write(COM1, " ppid=");
        serial_write_dec(COM1, (uint32_t)proc_table[dying].parent_pid);
        serial_write(COM1, " state=");
        serial_write(COM1, (proc_table[dying].state == PROC_DEAD) ? "DEAD" : "ZOMBIE");
        serial_write(COM1, " (exit)\n");
    }

    int next = sched_next_after(dying >= 0 ? dying : 0);
    if (next >= 0) {
        current_proc = -1;
        do_switch(next, regs);
        return;
    }

    /* Check for sleeping processes */
    int sleeper = -1;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_SLEEPING) {
            if (sleeper < 0 ||
                proc_table[i].sleep_until < proc_table[sleeper].sleep_until)
                sleeper = i;
        }
    }

    if (sleeper >= 0) {
        current_proc = -1;
        while (pit_ticks() < proc_table[sleeper].sleep_until)
            __asm__ volatile("hlt");
        proc_table[sleeper].state = PROC_RUNNABLE;
        do_switch(sleeper, regs);
        return;
    }

    current_proc   = -1;
    process_exited = 1;

    proc_dump_table("last-exit");

    if (kernel_esp != 0) {
        /* Blocking exec_elf() path — exit_trampoline restores the kernel stack
         * saved by launch_ring3 and returns into exec_elf(). */
        serial_write(COM1, "[proc] restoring via exit_trampoline\n");
        regs->eip    = (uint32_t)exit_trampoline;
        regs->cs     = SEG_KCODE;
        regs->eflags = 0x02;
    } else if (ring0_has_saved_context) {
        /* Non-blocking exec_elf_spawn() path — restore the ring-0 shell's ISR
         * frame so IRET resumes the hlt loop inside console_read_line_opts.
         *
         * When the PIT fired in ring-0 (sti;hlt), only EIP/CS/EFLAGS were
         * pushed — useresp/ss are NOT part of the ring-0 interrupt frame.
         * do_switch wrote all 76 bytes of ring-3 context onto the frame,
         * landing ring-3 useresp/ss at E+0 and E+4, overwriting live ring-0
         * stack data (e.g. keyboard_read_event's return address).
         *
         * saved_ring0_regs was captured BEFORE do_switch ran, so
         * saved_ring0_regs.useresp/ss hold the original ring-0 values.
         * Restore them, rebuild the IRET frame, and resume via trampoline. */
        serial_write(COM1, "[proc] restoring ring-0 context\n");
        wm_cleanup_all_user_windows();
        kmalloc_reset();
        paging_reset_phys_heap();
        ring0_has_saved_context = 0;

        /* saved_ring0_regs.esp = E-20 (pusha saves pre-pusha ESP).
         * E = the ring-0 stack pointer at the moment the interrupt fired. */
        uint32_t ring0_E = saved_ring0_regs.esp + 20u;

        /* Rebuild the 3-item IRET frame at E-12 (do_switch overwrote it). */
        uint32_t *iret_slot = (uint32_t *)(ring0_E - 12u);
        iret_slot[0] = saved_ring0_regs.eip;
        iret_slot[1] = 0x08u;                    /* CS = kernel code */
        iret_slot[2] = saved_ring0_regs.eflags;

        /* Repair the two ring-0 stack words above the IRET frame that
         * do_switch corrupted with ring-3's useresp/ss. */
        *(uint32_t *)(ring0_E + 0u) = saved_ring0_regs.useresp;
        *(uint32_t *)(ring0_E + 4u) = saved_ring0_regs.ss;

        ring0_resume_target_esp = (uint32_t)iret_slot;

        /* Restore ring-0 general-purpose registers and redirect EIP through
         * the trampoline, which will switch ESP to the ring-0 stack and iret
         * back to the original ring-0 execution point. */
        *regs = saved_ring0_regs;
        regs->eip    = (uint32_t)ring0_resume_trampoline;
        regs->cs     = 0x08u;
        regs->eflags = 0x02u;  /* IF=0; trampoline does cli before iret */
    } else {
        /* No saved context (e.g. very early crash) — fire trampoline anyway;
         * kernel_esp==0 will triple-fault but that is the existing behaviour. */
        regs->eip    = (uint32_t)exit_trampoline;
        regs->cs     = SEG_KCODE;
        regs->eflags = 0x02;
    }
}

int proc_fork(registers_t *regs) {
    if (current_proc < 0) return -EAGAIN;

    int child_slot = alloc_child_slot();
    if (child_slot < 0) {
        klog("proc", "fork: no free slots");
        return -EAGAIN;
    }

    process_t *parent = &proc_table[current_proc];
    process_t *child  = &proc_table[child_slot];

    /*
     * Allocate 256 physical pages for child's user space (0x300000-0x3FFFFF).
     * We need 256 pages of 4KB = 1MB.
     */
    uint32_t child_phys_base = paging_alloc_phys_page();
    if (!child_phys_base) {
        klog("proc", "fork: out of physical pages");
        child->state = PROC_DEAD;
        return -ENOMEM;
    }
    /* Allocate the remaining 255 pages to fill the 1MB region */
    for (int p = 1; p < 256; p++) {
        uint32_t page = paging_alloc_phys_page();
        if (!page) {
            klog("proc", "fork: out of physical pages during alloc");
            child->state = PROC_DEAD;
            return -ENOMEM;
        }
    }

    /* Copy parent user space (0x300000..0x3FFFFF) to child's physical pages */
    uint8_t *src = (uint8_t *)USER_BASE;
    uint8_t *dst = (uint8_t *)child_phys_base;
    uint32_t region = USER_STACK - USER_BASE;  /* 1 MB */
    for (uint32_t i = 0; i < region; i++)
        dst[i] = src[i];

    /* Build child's page directory */
    paging_clone(proc_pdirs[child_slot], proc_ptabs[child_slot],
                 proc_ptabs1[child_slot], child_phys_base);
    child->page_dir = proc_pdirs[child_slot];

    /* Clone fd table from parent (bumping pipe refcounts) */
    fd_table_clone(&child->fd_table, &parent->fd_table, 0 /* don't close cloexec on fork */);

    /* Clone CPU state; child returns 0 from fork */
    child->saved_regs     = *regs;
    child->saved_regs.eax = 0;

    child->state = PROC_RUNNABLE;

    serial_write(COM1, "[PROC] fork parent_pid=");
    serial_write_dec(COM1, (uint32_t)parent->pid);
    serial_write(COM1, " child_pid=");
    serial_write_dec(COM1, (uint32_t)child->pid);
    serial_write(COM1, " child_slot=");
    serial_write_dec(COM1, (uint32_t)child_slot);
    serial_write(COM1, " phys=");
    serial_write_hex(COM1, child_phys_base);
    serial_write(COM1, "\n");

    return child->pid;
}

pid_t proc_waitpid(pid_t pid, int *status, int options, registers_t *regs) {
    if (current_proc < 0) return -ECHILD;

    pid_t my_pid = proc_table[current_proc].pid;

    /* First scan for zombie children matching pid */
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state != PROC_ZOMBIE) continue;
        if (proc_table[i].parent_pid != my_pid) continue;
        if (pid > 0 && proc_table[i].pid != pid) continue;
        if (pid < -1 && proc_table[i].pgid != -pid) continue;

        /* Found a zombie to reap */
        pid_t child_pid = proc_table[i].pid;
        if (status) *status = proc_table[i].exit_status;
        proc_table[i].state = PROC_DEAD;
        serial_write(COM1, "[PROC] waitpid: reaped child pid=");
        serial_write_dec(COM1, (uint32_t)child_pid);
        serial_write(COM1, " by pid=");
        serial_write_dec(COM1, (uint32_t)my_pid);
        serial_write(COM1, "\n");
        return child_pid;
    }

    /* Check if any matching children exist */
    int has_child = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].parent_pid != my_pid) continue;
        if (proc_table[i].state == PROC_DEAD) continue;
        if (pid > 0 && proc_table[i].pid != pid) continue;
        if (pid < -1 && proc_table[i].pgid != -pid) continue;
        has_child = 1;
        break;
    }

    if (!has_child) return -ECHILD;

    if (options & WNOHANG) return 0;

    /* Block this process until a child exits.
     *
     * Use the eip-2 re-entry pattern (same as proc_block_on_kbd): subtract 2
     * from the saved EIP so that when proc_exit() wakes us, re-executing
     * int $0x80 re-enters waitpid and hits the zombie-scan above.
     *
     * CRITICAL: capture the waiter slot BEFORE calling do_switch().
     * do_switch() changes current_proc to the scheduled process.  The old
     * "goto retry" pattern ran with current_proc = child, which accidentally
     * blocked the child (PROC_BLOCKED), triggered deadlock-avoidance, then
     * returned -ECHILD as the child's fork() return value.  The child took
     * the "fork failed" error path, ran forever as an orphan term.elf clone
     * that could not receive keyboard events, and the parent stayed
     * PROC_BLOCKED forever. */
    int waiter = current_proc;

    serial_write(COM1, "[PROC] pid=");
    serial_write_dec(COM1, (uint32_t)proc_table[waiter].pid);
    serial_write(COM1, " ppid=");
    serial_write_dec(COM1, (uint32_t)proc_table[waiter].parent_pid);
    serial_write(COM1, " state=BLOCKED reason=WAITPID\n");

    proc_table[waiter].saved_regs     = *regs;
    proc_table[waiter].saved_regs.eip -= 2;   /* re-execute int $0x80 on wakeup */
    proc_table[waiter].state          = PROC_BLOCKED;

    int next = sched_next_after(waiter);
    if (next < 0) {
        /* No other runnable process — undo block and let caller handle */
        proc_table[waiter].state          = PROC_RUNNING;
        proc_table[waiter].saved_regs.eip += 2;
        return -ECHILD;
    }

    do_switch(next, regs);
    /* IRET takes execution to the next process.  The syscall handler stores
     * our return value (0) in regs->eax, which is correct: fork() returns 0
     * to the child.  When the child exits, proc_exit() makes the waiter
     * RUNNABLE; it re-executes int $0x80 and hits the zombie-scan above. */
    return 0;
}

/* Legacy proc_wait wrapper */
int proc_wait(registers_t *regs) {
    int status = 0;
    pid_t ret = proc_waitpid(-1, &status, 0, regs);
    if (ret < 0) return -1;
    return status;
}

void proc_sleep(registers_t *regs, uint32_t ticks) {
    if (current_proc < 0) return;

    if (ticks == 0) {
        regs->eax = 0;
        return;
    }

    uint32_t wake_at = pit_ticks() + ticks;
    proc_table[current_proc].sleep_until = wake_at;

    int next = sched_next_after(current_proc);
    if (next < 0) {
        while (pit_ticks() < wake_at)
            __asm__ volatile("hlt");
        regs->eax = 0;
        return;
    }

    proc_table[current_proc].saved_regs     = *regs;
    proc_table[current_proc].saved_regs.eax = 0;
    proc_table[current_proc].state          = PROC_SLEEPING;

    do_switch(next, regs);
}

int proc_send_signal(pid_t pid, int sig) {
    if (sig <= 0 || sig >= NSIG) return -EINVAL;

    int idx = proc_find_by_pid(pid);
    if (idx < 0) return -ESRCH;

    if (sig == SIGKILL) {
        /* Immediately terminate */
        if (idx == current_proc) {
            /* Will be handled by caller */
            proc_table[idx].sig_pending |= (1U << sig);
        } else {
            proc_table[idx].state       = PROC_ZOMBIE;
            proc_table[idx].exit_status = W_SIGNALED(SIGKILL);
            /* Wake parent if blocked */
            pid_t ppid = proc_table[idx].parent_pid;
            for (int i = 0; i < MAX_PROCS; i++) {
                if (proc_table[i].pid == ppid && proc_table[i].state == PROC_BLOCKED) {
                    proc_table[i].saved_regs.eax = (uint32_t)proc_table[idx].pid;
                    proc_table[idx].state        = PROC_DEAD;
                    proc_table[i].state          = PROC_RUNNABLE;
                    break;
                }
            }
        }
        return 0;
    }

    proc_table[idx].sig_pending |= (1U << sig);

    /* Wake sleeping/blocked process */
    if (proc_table[idx].state == PROC_SLEEPING ||
        proc_table[idx].state == PROC_BLOCKED) {
        proc_table[idx].state = PROC_RUNNABLE;
    }

    return 0;
}

void proc_send_signal_group(pgid_t pgid, int sig) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state != PROC_DEAD &&
            proc_table[i].state != PROC_ZOMBIE &&
            proc_table[i].pgid == pgid) {
            proc_send_signal(proc_table[i].pid, sig);
        }
    }
}

static void proc_default_action(int sig, registers_t *regs) {
    switch (sig) {
    case SIGCHLD:
    case SIGURG:
    case SIGWINCH:
        /* Ignore by default */
        return;
    case SIGCONT:
        if (current_proc >= 0 && proc_table[current_proc].state == PROC_STOPPED) {
            proc_table[current_proc].state = PROC_RUNNING;
        }
        return;
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
        if (current_proc >= 0) {
            proc_table[current_proc].saved_regs = *regs;
            proc_table[current_proc].state      = PROC_STOPPED;
            int next = sched_next_after(current_proc);
            if (next >= 0) do_switch(next, regs);
        }
        return;
    default:
        /* Terminate */
        proc_exit(regs, W_SIGNALED(sig));
        return;
    }
}

void proc_deliver_signals(registers_t *regs) {
    if (current_proc < 0) return;
    /* Only deliver when returning to user space */
    if ((regs->cs & 3) != 3) return;

    process_t *proc = &proc_table[current_proc];
    uint32_t deliverable = proc->sig_pending & ~proc->sig_mask;
    if (!deliverable) return;

    /* Find lowest set bit */
    int sig = -1;
    for (int s = 1; s < NSIG; s++) {
        if (deliverable & (1U << s)) {
            sig = s;
            break;
        }
    }
    if (sig < 0) return;

    /* Clear from pending */
    proc->sig_pending &= ~(1U << sig);

    struct sigaction *sa = &proc->sig_actions[sig];

    if (sa->sa_handler == SIG_DFL) {
        proc_default_action(sig, regs);
        return;
    }
    if (sa->sa_handler == SIG_IGN) {
        return;
    }

    /* Custom handler: build sig_frame_t on user stack */
    uint32_t user_sp = regs->useresp;
    user_sp -= sizeof(sig_frame_t);
    user_sp &= ~3U;  /* align to 4 bytes */

    /* Refuse to deliver if the signal frame would land outside user space —
     * that would write into supervisor memory or an unmapped page. */
    if (user_sp < USER_BASE || user_sp + sizeof(sig_frame_t) > USER_STACK) {
        /* Stack overflow during signal delivery: kill the process cleanly */
        proc_exit(regs, W_SIGNALED(SIGSEGV));
        return;
    }

    sig_frame_t *frame = (sig_frame_t *)user_sp;
    frame->retaddr       = SIG_TRAMPOLINE_ADDR;
    frame->signum        = sig;
    frame->saved_eax     = regs->eax;
    frame->saved_ecx     = regs->ecx;
    frame->saved_edx     = regs->edx;
    frame->saved_ebx     = regs->ebx;
    frame->saved_esi     = regs->esi;
    frame->saved_edi     = regs->edi;
    frame->saved_ebp     = regs->ebp;
    frame->saved_eip     = regs->eip;
    frame->saved_eflags  = regs->eflags;
    frame->saved_useresp = regs->useresp;
    frame->saved_mask    = proc->sig_mask;

    /* Mask signals during handler */
    proc->sig_mask |= sa->sa_mask | (1U << sig);

    /* Redirect to handler */
    regs->eip    = (uint32_t)sa->sa_handler;
    regs->useresp = user_sp;

    if (sa->sa_flags & SA_RESETHAND) {
        sa->sa_handler = SIG_DFL;
    }
}

void proc_timer_tick(registers_t *regs) {
    uint32_t now = pit_ticks();
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_SLEEPING && now >= proc_table[i].sleep_until)
            proc_table[i].state = PROC_RUNNABLE;
    }

    if (current_proc < 0) {
        /* Ring-0 shell is active.  Check for any runnable ring-3 process. */
        for (int i = 0; i < MAX_PROCS; i++) {
            if (proc_table[i].state == PROC_RUNNABLE) {
                /* Always save the current ring-0 ISR frame before switching.
                 * This keeps saved_ring0_regs up-to-date on every yield so
                 * that proc_exit restores the LATEST ring-0 position, not a
                 * stale snapshot from the very first switch. */
                saved_ring0_regs = *regs;
                ring0_has_saved_context = 1;
                do_switch(i, regs);
                return;
            }
        }
        return;
    }

    /* current_proc >= 0: a ring-3 process is running. */

    /* Interrupted inside a kernel (ring-0) path — never preempt. */
    if ((regs->cs & 3) != 3) return;

    if (proc_table[current_proc].ticks_remaining > 0)
        proc_table[current_proc].ticks_remaining--;

    if (proc_table[current_proc].ticks_remaining > 0)
        return;

    /* Timeslice exhausted.  Try another ring-3 process first. */
    int next = sched_next_after(current_proc);
    if (next >= 0) {
        proc_table[current_proc].saved_regs = *regs;
        proc_table[current_proc].state      = PROC_RUNNABLE;
        do_switch(next, regs);
        return;
    }

    /* No other ring-3 process is runnable.
     * Yield to the ring-0 shell so STerm / console_read_line can process
     * keyboard input and update its display.  Ring-0 runs briefly (until its
     * own hlt wakes on the next timer tick) then switches back to ring-3.
     *
     * IMPORTANT: we must use ring0_resume_trampoline here, same as proc_exit.
     *
     * When the PIT originally fired in ring-0 (during sti;hlt;cli),
     * do_switch() wrote 76 bytes of ring-3 context over the ring-0 ISR frame.
     * The ring-0 ISR frame is only 68 bytes (no useresp/ss for ring-0→ring-0
     * interrupts), so ring-3's useresp and ss landed at ring0_E+0 and ring0_E+4,
     * corrupting whatever was above the ring-0 interrupt frame on the ring-0
     * kernel stack (e.g. keyboard_read_event's caller return address).
     *
     * saved_ring0_regs holds the pre-corruption snapshot.  We:
     *   1. Repair the two corrupted words above the ring-0 IRET frame.
     *   2. Rebuild the 3-word IRET frame (EIP/CS/EFLAGS) at ring0_E-12.
     *   3. Set ring0_resume_target_esp to point at the rebuilt frame.
     *   4. Redirect execution through ring0_resume_trampoline, which does
     *         movl ring0_resume_target_esp, %esp; iret
     *      This switches from the ring-3 kernel stack back to the correct
     *      ring-0 stack position before executing IRET, restoring the ring-0
     *      shell with the correct ESP. */
    if (ring0_has_saved_context) {
        proc_table[current_proc].saved_regs = *regs;
        proc_table[current_proc].state      = PROC_RUNNABLE;
        current_proc = -1;

        /* ring0_E = original ESP value at the moment the ring-0 PIT fired.
         * saved_ring0_regs.esp = ESP at pusha time = ring0_E - 20. */
        uint32_t ring0_E    = saved_ring0_regs.esp + 20u;
        uint32_t *iret_slot = (uint32_t *)(ring0_E - 12u);

        /* Rebuild 3-word IRET frame on the ring-0 stack. */
        iret_slot[0] = saved_ring0_regs.eip;
        iret_slot[1] = 0x08u;   /* CS = kernel code segment */
        iret_slot[2] = saved_ring0_regs.eflags;

        /* Repair the two words above the IRET frame that do_switch corrupted
         * with ring-3's useresp/ss. */
        *(uint32_t *)(ring0_E + 0u) = saved_ring0_regs.useresp;
        *(uint32_t *)(ring0_E + 4u) = saved_ring0_regs.ss;

        ring0_resume_target_esp = (uint32_t)iret_slot;

        /* Restore ring-0 GP registers and redirect EIP through the trampoline
         * so it switches ESP to the ring-0 stack before executing IRET. */
        *regs        = saved_ring0_regs;
        regs->eip    = (uint32_t)ring0_resume_trampoline;
        regs->cs     = 0x08u;
        regs->eflags = 0x02u;  /* IF=0; trampoline does cli before iret */

        serial_write(COM1, "[sched] yield ring3→ring0 (trampoline)\n");
        return;
    }

    /* Fallback: no ring-0 context (shouldn't happen in spawn-only path). */
    proc_table[current_proc].ticks_remaining = PROC_TIMESLICE;
}

/*
 * Block the current process waiting for data in pipe pipe_idx.
 * We save EIP-2 so that when the process is woken it re-executes int $0x80
 * and retries the read syscall from scratch.
 */
void proc_block_on_pipe(int pipe_idx, registers_t *regs) {
    if (current_proc < 0) return;

    proc_table[current_proc].pipe_wait_idx  = pipe_idx;
    proc_table[current_proc].saved_regs     = *regs;
    proc_table[current_proc].saved_regs.eip -= 2;  /* re-execute int $0x80 */
    proc_table[current_proc].state          = PROC_BLOCKED;

    int next = sched_next_after(current_proc);
    if (next < 0) {
        /* No other process to run — busy-spin by staying runnable */
        proc_table[current_proc].state = PROC_RUNNABLE;
        return;
    }
    do_switch(next, regs);
}

/* ================================================================
 * Keyboard blocking — used by sys_linux_read(fd=0) so that a process
 * waiting for stdin yields the CPU to GUI processes instead of
 * busy-spinning in ring-0 via keyboard_read_event.
 *
 * pipe_wait_idx == -2 is the sentinel for "blocked on keyboard".
 * ================================================================ */
#define KBD_WAIT_SENTINEL  (-2)

void proc_block_on_kbd(registers_t *regs) {
    if (current_proc < 0) return;

    proc_table[current_proc].pipe_wait_idx  = KBD_WAIT_SENTINEL;
    proc_table[current_proc].saved_regs     = *regs;
    proc_table[current_proc].saved_regs.eip -= 2;   /* re-execute int $0x80 */
    proc_table[current_proc].state          = PROC_BLOCKED;

    int next = sched_next_after(current_proc);
    if (next < 0) {
        /* No other runnable process — fall back to staying alive */
        proc_table[current_proc].state          = PROC_RUNNABLE;
        proc_table[current_proc].pipe_wait_idx  = -1;
        return;
    }
    do_switch(next, regs);
}

void proc_wake_kbd_waiters(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state          == PROC_BLOCKED &&
            proc_table[i].pipe_wait_idx  == KBD_WAIT_SENTINEL) {
            proc_table[i].state         = PROC_RUNNABLE;
            proc_table[i].pipe_wait_idx = -1;
        }
    }
}

/* ================================================================
 * Concurrent ELF spawning
 * ================================================================ */

int proc_find_spawn_slot(void) {
    for (int i = 1; i < MAX_PROCS; i++)
        if (proc_table[i].state == PROC_DEAD) return i;
    return -1;
}

/*
 * Set up process slot `slot` as a fresh ring-3 user process and make it
 * PROC_RUNNABLE.  The preemptive timer will switch to it at the next tick.
 *
 * phys_user_base — physical (= supervisor virtual) address of the 1 MB
 *   block that will back 0x300000–0x3FFFFF in the new process's page dir.
 *   The caller has already copied ELF segments there.
 */
int proc_spawn_user(uint32_t entry, uint32_t user_sp,
                    int slot, uint32_t phys_user_base)
{
    if (slot < 1 || slot >= MAX_PROCS) return -1;
    if (proc_table[slot].state != PROC_DEAD)  return -1;

    process_t *p = &proc_table[slot];

    /* Identity */
    p->pid             = slot + 1;
    p->parent_pid      = -1;          /* independent — not a fork() child */
    p->pgid            = slot + 1;
    p->sid             = slot + 1;
    p->uid  = p->euid  = 0;
    p->gid  = p->egid  = 0;
    p->umask           = 022;
    p->exit_status     = 0;
    p->ticks_remaining = PROC_TIMESLICE;
    p->brk             = USER_STACK;  /* spawned apps don't use sbrk */
    p->pipe_wait_idx   = -1;
    p->sig_pending     = 0;
    p->sig_mask        = 0;
    p->cwd[0] = '/'; p->cwd[1] = '\0';
    for (int s = 0; s < NSIG; s++) {
        p->sig_actions[s].sa_handler = SIG_DFL;
        p->sig_actions[s].sa_flags   = 0;
        p->sig_actions[s].sa_mask    = 0;
    }
    fd_table_init(&p->fd_table);
    /* Pre-wire stdin/stdout/stderr so the first open() gets fd=3, not fd=0.
     * Without this, fopen() returns fd=0 and read(0,...) hits the stdin
     * special-case, turning every file read into a keyboard read. */
    fd_alloc_tty(&p->fd_table, O_RDONLY);  /* fd 0 = stdin  */
    fd_alloc_tty(&p->fd_table, O_WRONLY);  /* fd 1 = stdout */
    fd_alloc_tty(&p->fd_table, O_WRONLY);  /* fd 2 = stderr */

    /* Page directory: kernel layout + user region remapped to phys_user_base */
    paging_clone(proc_pdirs[slot], proc_ptabs[slot],
                 proc_ptabs1[slot], phys_user_base);
    p->page_dir = proc_pdirs[slot];

    /*
     * Initial CPU state for IRET into ring-3.
     * The ISR pops registers_t in order then executes IRET.
     * Fields that matter: eip, cs, eflags, useresp, ss.
     * Segment regs ds/es/fs/gs are restored by the ISR's pop instructions.
     */
    registers_t *r = &p->saved_regs;
    r->ds = r->es = r->fs = r->gs = 0x23;  /* user data DPL=3 */
    r->ss         = 0x23;
    r->cs         = 0x1B;   /* user code DPL=3 */
    r->eip        = entry;
    r->useresp    = user_sp;
    r->eflags     = 0x200;  /* IF=1 — enable timer preemption in ring3 */
    r->eax = r->ebx = r->ecx = r->edx = 0;
    r->esi = r->edi = r->ebp = r->esp  = 0;
    r->int_no = r->err_code = 0;

    /* Mark runnable — do_switch() will call tss_set_esp0(proc_kstacks[slot])
     * when the scheduler actually runs this process. */
    p->state = PROC_RUNNABLE;

    serial_write(COM1, "[proc] spawn pid=");
    serial_write_dec(COM1, (uint32_t)p->pid);
    serial_write(COM1, " slot=");
    serial_write_dec(COM1, (uint32_t)slot);
    serial_write(COM1, " phys=");
    serial_write_hex(COM1, phys_user_base);
    serial_write(COM1, "\n");

    return slot;
}

/* Wake any process blocked on pipe pipe_idx. */
void proc_wake_pipe_waiters(int pipe_idx) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_BLOCKED &&
            proc_table[i].pipe_wait_idx == pipe_idx) {
            proc_table[i].pipe_wait_idx  = -1;
            proc_table[i].state          = PROC_RUNNABLE;
        }
    }
}
