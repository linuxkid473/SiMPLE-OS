#ifndef SIMPLE_SIGNAL_H
#define SIMPLE_SIGNAL_H
#include "types.h"

/* Signal numbers (Linux-compatible) */
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31

#define NSIG      32

#define SIG_DFL   ((void(*)(int))0)
#define SIG_IGN   ((void(*)(int))1)
#define SIG_ERR   ((void(*)(int))-1)

/* sigaction flags */
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

/* sigprocmask how */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef uint32_t sigset_t;

struct sigaction {
    void     (*sa_handler)(int);
    uint32_t   sa_flags;
    void     (*sa_restorer)(void);
    sigset_t   sa_mask;
};

/* Signal frame pushed on user stack when delivering a signal */
typedef struct {
    uint32_t retaddr;     /* points to sigreturn trampoline */
    int      signum;
    uint32_t saved_eax;
    uint32_t saved_ecx;
    uint32_t saved_edx;
    uint32_t saved_ebx;
    uint32_t saved_esi;
    uint32_t saved_edi;
    uint32_t saved_ebp;
    uint32_t saved_eip;
    uint32_t saved_eflags;
    uint32_t saved_useresp;
    sigset_t saved_mask;
} __attribute__((packed)) sig_frame_t;

/* Addresses in user space for kernel-planted stubs.
 *
 * Memory map (top of user region, high → low):
 *
 *   0x3FFFFF  ← top of user region
 *   0x3FFFF0  ← EXIT_STUB_ADDR   (10 bytes: mov $1,%eax; xor %ebx,%ebx; int $0x80)
 *   0x3FFFE8  ← SIG_TRAMPOLINE_ADDR (8 bytes: mov $119,%eax; int $0x80; hlt)
 *   0x3FFFE0  ← USER_INITIAL_SP  (16-byte aligned; stack grows DOWN from here)
 *
 *   Stubs live ABOVE USER_INITIAL_SP so the downward-growing stack can never
 *   reach them regardless of call depth.
 *
 *   Initial stack built by build_posix_stack() (grows down from USER_INITIAL_SP):
 *     [esp]   = EXIT_STUB_ADDR  ← fake return addr (ret from _start → clean exit)
 *     [esp+4] = argc
 *     [esp+8] = argv[0]  ...
 */
#define EXIT_STUB_ADDR       0x3FFFF0U   /* exit stub (above initial SP) */
#define SIG_TRAMPOLINE_ADDR  0x3FFFE8U   /* sigreturn trampoline (above initial SP) */
#define USER_INITIAL_SP      0x3FFFE0U   /* initial user stack pointer */

#endif
