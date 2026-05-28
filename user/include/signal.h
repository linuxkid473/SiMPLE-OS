#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

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
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22

#define NSIG      32

typedef unsigned int sigset_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

/* SA flags */
#define SA_RESTART   0x10000000
#define SA_NOCLDSTOP 0x00000001
#define SA_SIGINFO   0x00000004
#define SA_RESETHAND 0x80000000

/* Field order matches kernel/include/signal.h and libc.c sigaction_user */
struct sigaction {
    void     (*sa_handler)(int);
    int        sa_flags;
    void     (*sa_restorer)(void);
    sigset_t   sa_mask;
};

/* sigprocmask how values */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

void (*signal(int sig, void (*handler)(int)))(int);
int   kill(int pid, int sig);
int   raise(int sig);
int   sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int   sigprocmask(int how, const sigset_t *set, sigset_t *old);

#endif
