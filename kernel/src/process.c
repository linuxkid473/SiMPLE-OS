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

process_t proc_table[MAX_PROCS];
int       current_proc = -1;

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
        proc_table[i].sig_pending = 0;
        proc_table[i].sig_mask   = 0;
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

    kmalloc_reset();

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

static int proc_alive_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_RUNNING ||
            proc_table[i].state == PROC_RUNNABLE)
            n++;
    }
    return n;
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

void proc_exit(registers_t *regs, int status) {
    int dying = current_proc;

    if (dying >= 0) {
        /* Close all fds */
        for (int fd = 0; fd < FD_MAX; fd++)
            if (proc_table[dying].fd_table.fds[fd].type != FD_NONE)
                fd_close(&proc_table[dying].fd_table, fd);

        proc_table[dying].state       = PROC_ZOMBIE;
        proc_table[dying].exit_status = status;

        /* Send SIGCHLD to parent; if parent is blocked in wait, reap and wake */
        pid_t ppid = proc_table[dying].parent_pid;
        if (ppid >= 0) {
            for (int i = 0; i < MAX_PROCS; i++) {
                if (proc_table[i].pid == ppid) {
                    /* Send SIGCHLD */
                    proc_table[i].sig_pending |= (1U << SIGCHLD);

                    if (proc_table[i].state == PROC_BLOCKED) {
                        proc_table[dying].state          = PROC_DEAD;
                        proc_table[i].saved_regs.eax     = (uint32_t)proc_table[dying].pid;
                        proc_table[i].state              = PROC_RUNNABLE;
                        klog_dec("proc", "exit: woke blocked parent", (uint32_t)ppid);
                    }
                    break;
                }
            }
        }

        serial_write(COM1, "[proc] exit pid=");
        serial_write_dec(COM1, (uint32_t)proc_table[dying].pid);
        serial_write(COM1, "\n");
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
    regs->eip    = (uint32_t)exit_trampoline;
    regs->cs     = SEG_KCODE;
    regs->eflags = 0x02;
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

    serial_write(COM1, "[proc] fork: child pid=");
    serial_write_dec(COM1, (uint32_t)child->pid);
    serial_write(COM1, " phys=");
    serial_write_hex(COM1, child_phys_base);
    serial_write(COM1, "\n");

    return child->pid;
}

pid_t proc_waitpid(pid_t pid, int *status, int options, registers_t *regs) {
    if (current_proc < 0) return -ECHILD;

    pid_t my_pid = proc_table[current_proc].pid;

retry:
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
        klog_dec("proc", "waitpid: reaped", (uint32_t)child_pid);
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

    /* Block until a child exits */
    proc_table[current_proc].saved_regs = *regs;
    proc_table[current_proc].state      = PROC_BLOCKED;

    int next = sched_next_after(current_proc);
    if (next < 0) {
        /* Deadlock avoidance */
        proc_table[current_proc].state = PROC_RUNNING;
        return -ECHILD;
    }

    do_switch(next, regs);
    /* When we wake, proc_exit set our saved_regs.eax to child pid.
     * The caller in idt.c will use regs->eax, so go retry to do a real reap. */
    goto retry;
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
        for (int i = 0; i < MAX_PROCS; i++) {
            if (proc_table[i].state == PROC_RUNNABLE) {
                do_switch(i, regs);
                return;
            }
        }
        return;
    }

    if ((regs->cs & 3) != 3) return;
    if (proc_alive_count() < 2) return;

    if (proc_table[current_proc].ticks_remaining > 0)
        proc_table[current_proc].ticks_remaining--;

    if (proc_table[current_proc].ticks_remaining > 0)
        return;

    int next = sched_next_after(current_proc);
    if (next < 0) {
        proc_table[current_proc].ticks_remaining = PROC_TIMESLICE;
        return;
    }

    proc_table[current_proc].saved_regs = *regs;
    proc_table[current_proc].state      = PROC_RUNNABLE;

    do_switch(next, regs);
}
