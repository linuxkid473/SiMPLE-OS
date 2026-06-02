/*
 * user/posixsmoke.c — comprehensive POSIX syscall smoke test for SiMPLE OS.
 *
 * Prints PASS: or FAIL: for each syscall/feature.  Continues on failures.
 * Final line: "PASS" (all pass) or "FAIL (<n>)" with failure count.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ---- raw Linux i386 int $0x80 wrappers ---- */
static inline long _sc0(long nr) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr) : "memory");
    return r;
}
static inline long _sc1(long nr, long a) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a) : "memory");
    return r;
}
static inline long _sc2(long nr, long a, long b) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b) : "memory");
    return r;
}
static inline long _sc3(long nr, long a, long b, long c) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static inline long _sc4(long nr, long a, long b, long c, long d) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}
static inline long _sc5(long nr, long a, long b, long c, long d, long e) {
    long r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return r;
}

/* ---- syscall wrappers not in user/include/ headers ---- */

static inline int smoke_gettid(void)           { return (int)_sc0(186); }
static inline int smoke_getuid(void)           { return (int)_sc0(199); }
static inline int smoke_getgid(void)           { return (int)_sc0(200); }
static inline int smoke_geteuid(void)          { return (int)_sc0(201); }
static inline int smoke_getegid(void)          { return (int)_sc0(202); }

static inline int smoke_wait4(int pid, int *st, int opts, void *rusage) {
    return (int)_sc4(114, (long)pid, (long)st, (long)opts, (long)rusage);
}

/* ---- local struct definitions (binary-compatible with kernel) ---- */

struct smoke_timeval  { long tv_sec; long tv_usec; };
struct smoke_timespec { int  tv_sec; int  tv_nsec; };
struct smoke_winsize  { uint16_t ws_row; uint16_t ws_col;
                        uint16_t ws_xpixel; uint16_t ws_ypixel; };
struct smoke_utsname  { char sysname[65]; char nodename[65];
                        char release[65]; char version[65]; char machine[65]; };
struct smoke_pollfd   { int fd; short events; short revents; };
struct smoke_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[1];
} __attribute__((packed));

/* ioctl with explicit arg (libc ioctl always passes arg=0) */
static inline int smoke_ioctl(int fd, unsigned long req, void *arg) {
    return (int)_sc3(54, (long)fd, (long)req, (long)arg);
}

/* gettimeofday */
static inline int smoke_gettimeofday(struct smoke_timeval *tv, void *tz) {
    return (int)_sc2(78, (long)tv, (long)tz);
}

/* clock_gettime */
static inline int smoke_clock_gettime(int clk, struct smoke_timespec *tp) {
    return (int)_sc2(265, (long)clk, (long)tp);
}

/* nanosleep — libc has it but uses its own struct timespec */
static inline int smoke_nanosleep(struct smoke_timespec *req, struct smoke_timespec *rem) {
    return (int)_sc2(162, (long)req, (long)rem);
}

/* sigpending via rt_sigpending (nr 175) */
static inline int smoke_sigpending(unsigned int *set) {
    return (int)_sc2(175, (long)set, (long)sizeof(unsigned int));
}

/* mmap / munmap — libc has them but without headers */
extern void *mmap(void *addr, int len, int prot, int flags, int fd, int off);
extern int   munmap(void *addr, int len);
extern void *sbrk(int inc);
extern int   uname(void *buf);
extern int   yield(void);

/* ---- test framework ---- */

static int fail_count = 0;

static void pass(const char *name) {
    printf("PASS: %s\n", name);
}

static void fail(const char *name, const char *reason) {
    printf("FAIL: %s\n", name);
    if (reason && reason[0])
        printf("  reason: %s\n", reason);
    fail_count++;
}

/* ---- signal handler state (must be volatile) ---- */

static volatile int sig_usr1_fired = 0;
static volatile int sig_usr2_fired = 0;

static void handle_usr1(int s) { (void)s; sig_usr1_fired = 1; }
static void handle_usr2(int s) { (void)s; sig_usr2_fired = 1; }

/* ---- fork memory-isolation witness ---- */
static volatile int fork_witness = 100;

/* ---- case-insensitive strcmp (FAT16 uppercases names) ---- */
static int ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ============================================================
 * Process subsystem
 * ============================================================ */

static void test_process(void) {
    printf("--- Process ---\n");

    /* getpid */
    int pid = (int)_sc0(20);
    if (pid > 0) pass("getpid");
    else fail("getpid", "returned <= 0");

    /* getppid */
    int ppid = (int)_sc0(64);
    if (ppid >= 0) pass("getppid");
    else fail("getppid", "returned < 0");

    /* gettid — kernel aliases to getpid */
    int tid = smoke_gettid();
    if (tid == pid) pass("gettid");
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "tid=%d pid=%d", tid, pid);
        fail("gettid", buf);
    }

    /* fork + wait4 + memory isolation */
    int saved = fork_witness;  /* 100 */
    int cpid = fork();
    if (cpid < 0) {
        fail("fork", "fork() returned < 0");
        fail("wait4", "skipped (fork failed)");
        fail("fork_isolation", "skipped");
    } else if (cpid == 0) {
        /* child: mutate witness then exit */
        fork_witness = 999;
        _exit(0);
    } else {
        /* parent */
        int status = -1;
        int wr = smoke_wait4(cpid, &status, 0, (void*)0);
        if (wr == cpid) pass("fork");
        else {
            char buf[64];
            snprintf(buf, sizeof(buf), "wait4 returned %d expected %d", wr, cpid);
            fail("fork", buf);
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) pass("wait4");
        else {
            char buf[64];
            snprintf(buf, sizeof(buf), "status=0x%x", (unsigned)status);
            fail("wait4", buf);
        }

        if (fork_witness == saved) pass("fork_isolation");
        else fail("fork_isolation", "child write visible in parent");
    }

    /* execve — fork child, execve smkhelp.elf which exits 42 */
    int epid = fork();
    if (epid < 0) {
        fail("execve", "fork() failed");
    } else if (epid == 0) {
        char *av[] = { "smkhelp.elf", (char*)0 };
        char *ev[] = { (char*)0 };
        execve("smkhelp.elf", av, ev);
        _exit(1); /* execve failed */
    } else {
        int st = -1;
        int wr = waitpid(epid, &st, 0);
        if (wr == epid && WIFEXITED(st) && WEXITSTATUS(st) == 42)
            pass("execve");
        else {
            char buf[64];
            snprintf(buf, sizeof(buf), "waited=%d exited=%d status=%d",
                     wr, WIFEXITED(st), WEXITSTATUS(st));
            fail("execve", buf);
        }
    }
}

/* ============================================================
 * File I/O subsystem
 * ============================================================ */

static void test_fileio(void) {
    printf("--- File I/O ---\n");

    /* open / close */
    int fd = open("smktest.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd >= 0) { pass("open"); close(fd); pass("close"); }
    else { fail("open", "O_CREAT|O_WRONLY failed"); fail("close", "skipped"); }

    /* write / read */
    fd = open("smkrw.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) { fail("write", "open failed"); fail("read", "skipped"); }
    else {
        int nw = write(fd, "RWDATA_OK", 9);
        close(fd);
        if (nw == 9) pass("write");
        else { char b[32]; snprintf(b,32,"wrote %d", nw); fail("write", b); }

        fd = open("smkrw.txt", O_RDONLY, 0);
        char rbuf[16] = {0};
        int nr = (fd >= 0) ? read(fd, rbuf, 16) : -1;
        if (fd >= 0) close(fd);
        if (nr == 9 && memcmp(rbuf, "RWDATA_OK", 9) == 0) pass("read");
        else { char b[32]; snprintf(b,32,"nr=%d",nr); fail("read", b); }
    }

    /* lseek */
    fd = open("smkseek.txt", O_RDWR | O_CREAT | O_TRUNC, 0);
    if (fd < 0) { fail("lseek", "open failed"); }
    else {
        write(fd, "SEEKTEST", 8);
        int p = lseek(fd, 0, SEEK_SET);
        char sb[16] = {0};
        int nr = read(fd, sb, 16);
        close(fd);
        if (p == 0 && nr == 8 && memcmp(sb, "SEEKTEST", 8) == 0) pass("lseek");
        else { char b[32]; snprintf(b,32,"lseek=%d nr=%d",p,nr); fail("lseek",b); }
    }

    /* dup */
    fd = open("smkdup.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) { fail("dup", "open failed"); }
    else {
        int fd2 = dup(fd);
        if (fd2 < 0) { close(fd); fail("dup", "dup() < 0"); }
        else {
            write(fd2, "DUP_OK", 6);
            close(fd); close(fd2);
            int rfd = open("smkdup.txt", O_RDONLY, 0);
            char b[16] = {0}; int nr = (rfd>=0)?read(rfd,b,16):-1;
            if (rfd>=0) close(rfd);
            if (nr==6 && memcmp(b,"DUP_OK",6)==0) pass("dup");
            else fail("dup", "data mismatch");
        }
    }

    /* dup2 */
    fd = open("smkdup2.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0) { fail("dup2", "open failed"); }
    else {
        int newfd = 9;
        int r = dup2(fd, newfd);
        if (r < 0) { close(fd); fail("dup2", "dup2() < 0"); }
        else {
            write(newfd, "DUP2_OK", 7);
            close(fd); close(newfd);
            int rfd = open("smkdup2.txt", O_RDONLY, 0);
            char b[16] = {0}; int nr = (rfd>=0)?read(rfd,b,16):-1;
            if (rfd>=0) close(rfd);
            if (nr==7 && memcmp(b,"DUP2_OK",7)==0) pass("dup2");
            else fail("dup2", "data mismatch");
        }
    }

    /* pipe — in-process transfer */
    {
        int pp[2];
        if (pipe(pp) != 0) { fail("pipe", "pipe() failed"); }
        else {
            write(pp[1], "PIPE_SMOKE", 10);
            close(pp[1]);
            char b[16] = {0};
            int nr = read(pp[0], b, 16);
            close(pp[0]);
            if (nr == 10 && memcmp(b, "PIPE_SMOKE", 10) == 0) pass("pipe");
            else { char msg[32]; snprintf(msg,32,"nr=%d",nr); fail("pipe",msg); }
        }
    }

    /* getcwd */
    {
        char cwdbuf[256];
        char *r = getcwd(cwdbuf, sizeof(cwdbuf));
        if (r && cwdbuf[0] != '\0') pass("getcwd");
        else fail("getcwd", "returned NULL or empty");
    }

    /* mkdir */
    {
        mkdir("smoktmp", 0755);  /* ignore error if already exists */
        int tfd = open("smoktmp", O_RDONLY, 0);
        if (tfd >= 0) { close(tfd); pass("mkdir"); }
        else fail("mkdir", "dir not accessible after mkdir");
    }

    /* rename */
    {
        int sfd = open("ren_src.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (sfd < 0) { fail("rename", "cannot create src"); }
        else {
            write(sfd, "R", 1); close(sfd);
            int r = rename("ren_src.txt", "ren_dst.txt");
            if (r != 0) { fail("rename", "rename() nonzero"); }
            else {
                int dst = open("ren_dst.txt", O_RDONLY, 0);
                int src = open("ren_src.txt", O_RDONLY, 0);
                int ok = (dst >= 0) && (src < 0);
                if (dst >= 0) close(dst);
                if (src >= 0) close(src);
                if (ok) pass("rename");
                else fail("rename", "dst missing or src still present");
            }
        }
    }

    /* unlink */
    {
        int ufd = open("smkdel.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (ufd < 0) { fail("unlink", "cannot create file"); }
        else {
            write(ufd, "D", 1); close(ufd);
            int r = unlink("smkdel.txt");
            if (r != 0) { fail("unlink", "unlink() nonzero"); }
            else {
                int chk = open("smkdel.txt", O_RDONLY, 0);
                if (chk < 0) pass("unlink");
                else { close(chk); fail("unlink", "file still exists"); }
            }
        }
    }

    /* getdents / getdents64 */
    {
        /* plant a marker file */
        int mfd = open("dent_mk.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (mfd >= 0) { write(mfd, "X", 1); close(mfd); }

        /* getdents (nr 141) — just verify > 0 bytes returned */
        int dfd = open("/", O_RDONLY, 0);
        if (dfd < 0) {
            fail("getdents",   "open('/') failed");
            fail("getdents64", "open('/') failed");
        } else {
            static char gbuf[2048];
            int n = (int)_sc3(141, (long)dfd, (long)gbuf, (long)sizeof(gbuf));
            close(dfd);
            if (n > 0) pass("getdents");
            else { char b[32]; snprintf(b,32,"returned %d",n); fail("getdents",b); }

            /* getdents64 (nr 220) — parse and find marker */
            dfd = open("/", O_RDONLY, 0);
            if (dfd < 0) { fail("getdents64", "second open('/') failed"); }
            else {
                static char g64buf[2048];
                int n64 = (int)_sc3(220, (long)dfd, (long)g64buf, (long)sizeof(g64buf));
                close(dfd);
                if (n64 <= 0) {
                    char b[32]; snprintf(b,32,"returned %d",n64);
                    fail("getdents64", b);
                } else {
                    int found = 0, pos = 0;
                    while (pos < n64) {
                        struct smoke_dirent64 *de =
                            (struct smoke_dirent64 *)(g64buf + pos);
                        if (de->d_reclen == 0) break;
                        if (ci_eq(de->d_name, "dent_mk.txt")) { found = 1; break; }
                        pos += de->d_reclen;
                    }
                    if (found) pass("getdents64");
                    else fail("getdents64", "marker file not found in listing");
                }
            }
        }
    }
}

/* ============================================================
 * Memory subsystem
 * ============================================================ */

static void test_memory(void) {
    printf("--- Memory ---\n");

    /* brk */
    {
        long cur = _sc1(45, 0L);
        if (cur <= 0) { fail("brk", "brk(0) returned <= 0"); }
        else {
            long nxt = _sc1(45, cur + 4096L);
            if (nxt > cur) pass("brk");
            else fail("brk", "brk extension did not advance");
        }
    }

    /* sbrk */
    {
        void *cur = sbrk(0);
        if (cur == (void*)-1 || cur == (void*)0) { fail("sbrk", "sbrk(0) failed"); }
        else {
            void *old = sbrk(4096);
            if (old == (void*)-1) { fail("sbrk", "sbrk(4096) failed"); }
            else {
                void *after = sbrk(0);
                if ((char*)after >= (char*)cur + 4096) pass("sbrk");
                else fail("sbrk", "heap did not grow");
            }
        }
    }

    /* mmap + munmap — anonymous private mapping */
    {
        /* PROT_READ|PROT_WRITE=3, MAP_PRIVATE|MAP_ANONYMOUS=0x22 */
        void *p = mmap((void*)0, 4096, 3, 0x22, -1, 0);
        if (p == (void*)-1 || p == (void*)0) {
            fail("mmap",   "mmap returned MAP_FAILED");
            fail("munmap", "skipped");
        } else {
            unsigned int *ip = (unsigned int *)p;
            ip[0] = 0xDEADC0DEU;
            ip[1] = 0xCAFEBABEU;
            if (ip[0] == 0xDEADC0DEU && ip[1] == 0xCAFEBABEU) pass("mmap");
            else fail("mmap", "pattern read-back mismatch");

            int r = munmap(p, 4096);
            if (r == 0) pass("munmap");
            else fail("munmap", "munmap returned nonzero");
        }
    }
}

/* ============================================================
 * Signals subsystem
 * ============================================================ */

static void test_signals(void) {
    printf("--- Signals ---\n");

    /* sigaction: install SIGUSR1 handler */
    struct sigaction sa;
    sa.sa_handler  = handle_usr1;
    sa.sa_flags    = 0;
    sa.sa_restorer = (void *)0;
    sa.sa_mask     = 0;
    int r = sigaction(SIGUSR1, &sa, (struct sigaction*)0);
    if (r == 0) pass("sigaction");
    else fail("sigaction", "sigaction() returned nonzero");

    /* kill + signal_delivery */
    sig_usr1_fired = 0;
    kill(getpid(), SIGUSR1);
    if (sig_usr1_fired) {
        pass("kill");
        pass("signal_delivery");
    } else {
        fail("kill",            "kill(self,SIGUSR1) returned but handler not fired");
        fail("signal_delivery", "SIGUSR1 handler did not execute");
    }

    /* sigprocmask + sigpending */
    struct sigaction sa2;
    sa2.sa_handler  = handle_usr2;
    sa2.sa_flags    = 0;
    sa2.sa_restorer = (void *)0;
    sa2.sa_mask     = 0;
    sigaction(SIGUSR2, &sa2, (struct sigaction*)0);

    unsigned int block_mask = 1U << (SIGUSR2 - 1);
    unsigned int old_mask   = 0;
    r = sigprocmask(SIG_BLOCK, &block_mask, &old_mask);
    if (r != 0) {
        fail("sigprocmask", "SIG_BLOCK failed");
        fail("sigpending",  "skipped");
        return;
    }

    sig_usr2_fired = 0;
    kill(getpid(), SIGUSR2);

    if (sig_usr2_fired) {
        /* signal was delivered despite the block — still pass but note anomaly */
        fail("sigprocmask", "blocked signal was delivered immediately");
    } else {
        /* good — signal is pending, not delivered */
        unsigned int pend = 0;
        smoke_sigpending(&pend);
        if (pend & block_mask) pass("sigpending");
        else fail("sigpending", "pending mask does not show SIGUSR2");

        /* unblock — kernel should deliver the pending signal */
        sigprocmask(SIG_UNBLOCK, &block_mask, (unsigned int *)0);
        yield();  /* ensure scheduler runs signal delivery */

        if (sig_usr2_fired) pass("sigprocmask");
        else fail("sigprocmask", "signal not delivered after unblock");
    }
}

/* ============================================================
 * Time subsystem
 * ============================================================ */

static void test_time(void) {
    printf("--- Time ---\n");

    /* gettimeofday */
    {
        struct smoke_timeval tv = {0, 0};
        int r = smoke_gettimeofday(&tv, (void*)0);
        if (r == 0) pass("gettimeofday");
        else fail("gettimeofday", "returned nonzero");
    }

    /* nanosleep — 1ms sleep */
    {
        struct smoke_timespec req = {0, 1000000};
        int r = smoke_nanosleep(&req, (struct smoke_timespec*)0);
        if (r == 0) pass("nanosleep");
        else fail("nanosleep", "returned nonzero");
    }

    /* clock_gettime — CLOCK_REALTIME = 0 */
    {
        struct smoke_timespec tp = {0, 0};
        int r = smoke_clock_gettime(0, &tp);
        if (r == 0) pass("clock_gettime");
        else { char b[32]; snprintf(b,32,"returned %d",r); fail("clock_gettime",b); }
    }
}

/* ============================================================
 * Terminal subsystem
 * ============================================================ */

static void test_terminal(void) {
    printf("--- Terminal ---\n");

    /* ioctl TCGETS (0x5401) on stdin */
    {
        struct {
            unsigned int c_iflag, c_oflag, c_cflag, c_lflag;
            unsigned char c_cc[19];
            unsigned int  _ispeed, _ospeed;
        } t;
        int r = smoke_ioctl(0, 0x5401, &t);
        if (r == 0) pass("ioctl_TCGETS");
        else { char b[32]; snprintf(b,32,"ioctl ret=%d",r); fail("ioctl_TCGETS",b); }
    }

    /* ioctl TIOCGWINSZ (0x5413) on stdin */
    {
        struct smoke_winsize ws = {0, 0, 0, 0};
        int r = smoke_ioctl(0, 0x5413, &ws);
        if (r == 0) pass("ioctl_TIOCGWINSZ");
        else { char b[32]; snprintf(b,32,"ioctl ret=%d",r); fail("ioctl_TIOCGWINSZ",b); }
    }
}

/* ============================================================
 * Poll subsystem
 * ============================================================ */

static void test_poll(void) {
    printf("--- Polling ---\n");

    /* Create pipe, write data, poll read end for POLLIN */
    int pp[2];
    if (pipe(pp) != 0) { fail("poll", "pipe() failed"); return; }

    write(pp[1], "POLL", 4);
    /* leave write end open so pipe has data */

    struct smoke_pollfd pfd;
    pfd.fd     = pp[0];
    pfd.events = 1; /* POLLIN */
    pfd.revents = 0;

    int r = (int)_sc3(168, (long)&pfd, 1L, 0L); /* timeout=0 */
    close(pp[0]); close(pp[1]);

    if (r == 1 && (pfd.revents & 1)) pass("poll");
    else { char b[64]; snprintf(b,64,"poll ret=%d revents=%d",r,(int)pfd.revents); fail("poll",b); }
}

/* ============================================================
 * Identity subsystem
 * ============================================================ */

static void test_identity(void) {
    printf("--- Identity ---\n");

    /* uname */
    {
        struct smoke_utsname u;
        memset(&u, 0, sizeof(u));
        int r = uname(&u);
        if (r == 0 && u.sysname[0] != '\0') pass("uname");
        else fail("uname", "returned nonzero or empty sysname");
    }

    /* getuid / geteuid / getgid / getegid */
    {
        int uid  = smoke_getuid();
        int euid = smoke_geteuid();
        int gid  = smoke_getgid();
        int egid = smoke_getegid();

        if (uid  >= 0) pass("getuid");  else fail("getuid",  "returned < 0");
        if (euid >= 0) pass("geteuid"); else fail("geteuid", "returned < 0");
        if (gid  >= 0) pass("getgid");  else fail("getgid",  "returned < 0");
        if (egid >= 0) pass("getegid"); else fail("getegid", "returned < 0");
    }
}

/* ============================================================
 * main
 * ============================================================ */

int main(void) {
    printf("=== posixsmoke ===\n");

    test_process();
    test_fileio();
    test_memory();
    test_signals();
    test_time();
    test_terminal();
    test_poll();
    test_identity();

    printf("====================\n");
    if (fail_count == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL (%d)\n", fail_count);
    }
    printf("====================\n");
    return fail_count;
}
