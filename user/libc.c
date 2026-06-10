/*
 * user/libc.c — user-space C library for SiMPLE OS.
 *
 * Uses Linux i386 syscall ABI: int $0x80
 *   eax = syscall number
 *   ebx = arg0
 *   ecx = arg1
 *   edx = arg2
 *   esi = arg3
 *   edi = arg4
 *   ebp = arg5
 */

/* ---- errno ---- */
static int _errno_storage = 0;

int *__errno_location(void) { return &_errno_storage; }
#define errno (*__errno_location())

/* ---- syscall wrappers ---- */
static inline long syscall0(long nr) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}

static inline long syscall1(long nr, long a) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a) : "memory");
    return ret;
}

static inline long syscall2(long nr, long a, long b) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b) : "memory");
    return ret;
}

static inline long syscall3(long nr, long a, long b, long c) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}

static inline long syscall4(long nr, long a, long b, long c, long d) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return ret;
}

static inline long syscall5(long nr, long a, long b, long c, long d, long e) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return ret;
}

/* ---- string functions ---- */
typedef unsigned int size_t;

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (n == (size_t)-1) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (char *)0;
}

char *strcat(char *dst, const char *src) {
    char *d = dst + strlen(dst);
    while (*src) *d++ = *src++;
    *d = '\0';
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    char *d = (char *)dst;
    while (n--) *d++ = (char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

/* ---- Process ---- */

void _exit(int code) {
    syscall1(1, code);
    for (;;) __asm__ volatile("hlt");
}

void exit(int code) {
    _exit(code);
}

int fork(void) {
    long ret = syscall0(2);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    long ret = syscall3(11, (long)path, (long)argv, (long)envp);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* exec — old-style (path only) via execve */
int exec(const char *path) {
    return execve(path, (char *const *)0, (char *const *)0);
}

/* execvp — no PATH on SiMPLE; try the name as given, then with ".elf".
 * environ is defined weakly here so legacy programs that link libc.c
 * without env.c still link; env.c's strong definition wins otherwise. */
char **environ __attribute__((weak)) = 0;
int execvp(const char *file, char *const argv[]) {
    execve(file, argv, environ);
    /* Only returns on failure — retry with .elf appended */
    char buf[64];
    size_t n = strlen(file);
    if (n < sizeof(buf) - 5) {
        strcpy(buf, file);
        strcpy(buf + n, ".elf");
        return execve(buf, argv, environ);
    }
    return -1;
}

int waitpid(int pid, int *status, int options) {
    long ret = syscall3(7, pid, (long)status, options);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int wait(int *status) {
    return waitpid(-1, status, 0);
}

int getpid(void) {
    return (int)syscall0(20);
}

int getppid(void) {
    return (int)syscall0(64);
}

int setsid(void) {
    long ret = syscall0(66);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int setpgid(int pid, int pgid) {
    long ret = syscall2(57, pid, pgid);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int getpgrp(void) {
    return (int)syscall0(65);
}

int kill(int pid, int sig) {
    long ret = syscall2(37, pid, sig);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* ---- File I/O ---- */

int open(const char *path, int flags, ...) {
    long ret = syscall3(5, (long)path, flags, 0644);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int close(int fd) {
    long ret = syscall1(6, fd);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int read(int fd, void *buf, int len) {
    long ret = syscall3(3, fd, (long)buf, len);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int write(int fd, const void *buf, int len) {
    long ret = syscall3(4, fd, (long)buf, len);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int lseek(int fd, int offset, int whence) {
    long ret = syscall3(19, fd, offset, whence);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* Legacy seek alias */
int seek(int fd, int offset, int whence) {
    return lseek(fd, offset, whence);
}

int dup(int fd) {
    long ret = syscall1(41, fd);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int dup2(int oldfd, int newfd) {
    long ret = syscall2(63, oldfd, newfd);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int pipe(int fds[2]) {
    long ret = syscall1(42, (long)fds);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int fcntl(int fd, int cmd, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    long arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    long ret = syscall3(55, fd, cmd, arg);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int ioctl(int fd, unsigned long req, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, req);
    unsigned long arg = __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    long ret = syscall3(54, fd, (long)req, (long)arg);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* Legacy fd_read / fd_write using fread/fwrite syscalls via Linux read/write */
int fd_read(int fd, void *buf, int len) {
    return read(fd, buf, len);
}

int fd_write(int fd, const void *buf, int len) {
    return write(fd, buf, len);
}

/* ---- Directories ---- */

int mkdir(const char *path, int mode) {
    long ret = syscall2(39, (long)path, mode);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int rmdir(const char *path) {
    long ret = syscall1(40, (long)path);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int unlink(const char *path) {
    long ret = syscall1(10, (long)path);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int rename(const char *old, const char *newp) {
    long ret = syscall2(38, (long)old, (long)newp);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int chdir(const char *path) {
    long ret = syscall1(12, (long)path);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

char *getcwd(char *buf, int len) {
    long ret = syscall2(183, (long)buf, len);
    if (ret < 0) { errno = (int)-ret; return (char *)0; }
    return buf;
}

/* ---- Memory ---- */

static int _current_brk = 0;

void *sbrk(int inc) {
    if (_current_brk == 0) {
        _current_brk = (int)syscall1(45, 0);
    }
    if (inc == 0) return (void *)_current_brk;

    int new_brk = (int)syscall1(45, _current_brk + inc);
    if (new_brk < 0) { errno = 12; return (void *)-1; }
    int old_brk = _current_brk;
    _current_brk = new_brk;
    return (void *)old_brk;
}

void *mmap(void *addr, int len, int prot, int flags, int fd, int off) {
    long ret = syscall5(192, (long)addr, len, prot, flags, fd);
    (void)off;
    if (ret < 0) { errno = (int)-ret; return (void *)-1; }
    return (void *)ret;
}

int munmap(void *addr, int len) {
    long ret = syscall2(91, (long)addr, len);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* ---- Signals ---- */

typedef unsigned int sigset_t;

/* struct sigaction layout must match kernel/include/signal.h:
 * { sa_handler, sa_flags, sa_restorer, sa_mask }
 * Define only when not already defined by user/include/signal.h. */
#ifndef _SIGNAL_H
struct sigaction {
    void     (*sa_handler)(int);
    unsigned   sa_flags;
    void     (*sa_restorer)(void);
    unsigned   sa_mask;   /* sigset_t = uint32_t */
};
#endif

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    long ret = syscall4(173, sig, (long)act, (long)oact, 4 /* sizeof sigset_t */);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oset) {
    long ret = syscall4(174, how, (long)set, (long)oset, sizeof(sigset_t));
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

void (*signal(int sig, void (*handler)(int)))(int) {
    struct sigaction act, oact;
    act.sa_handler = handler;
    act.sa_flags   = 0;
    act.sa_restorer = (void *)0;
    act.sa_mask    = 0;
    if (sigaction(sig, &act, &oact) < 0) return (void (*)(int))-1;
    return oact.sa_handler;
}

/* ---- TTY ---- */

typedef struct {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_cc[19];
    unsigned int  _ispeed;
    unsigned int  _ospeed;
} termios_t;

int isatty(int fd) {
    termios_t t;
    return (ioctl(fd, 0x5401, (unsigned long)&t) == 0) ? 1 : 0;
}

int tcgetattr(int fd, termios_t *t) {
    long ret = syscall3(54, fd, 0x5401, (long)t);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int tcsetattr(int fd, int action, const termios_t *t) {
    unsigned long req;
    if (action == 0) req = 0x5402;
    else if (action == 1) req = 0x5403;
    else req = 0x5404;
    long ret = syscall3(54, fd, (long)req, (long)t);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int tcgetpgrp(int fd) {
    int pgid = 0;
    long ret = syscall3(54, fd, 0x540F, (long)&pgid);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return pgid;
}

int tcsetpgrp(int fd, int pgid) {
    long ret = syscall3(54, fd, 0x5410, (long)&pgid);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* ---- Time ---- */

struct timespec {
    int tv_sec;
    int tv_nsec;
};

int nanosleep(const struct timespec *req, struct timespec *rem) {
    long ret = syscall2(162, (long)req, (long)rem);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

unsigned sleep(unsigned sec) {
    struct timespec req;
    req.tv_sec  = (int)sec;
    req.tv_nsec = 0;
    nanosleep(&req, (struct timespec *)0);
    return 0;
}

/* Legacy sleep in ticks */
int sleep_ticks(unsigned int ticks) {
    /* Use nanosleep with ticks*10ms */
    struct timespec req;
    req.tv_sec  = (int)(ticks / 100);
    req.tv_nsec = (int)((ticks % 100) * 10000000);
    return nanosleep(&req, (struct timespec *)0);
}

/* yield — cooperatively hand CPU to the next runnable process. */
int yield(void) {
    /* Use the high-alias yield */
    long ret = syscall0(500 + 4);
    return (int)ret;
}

/* ---- SiMPLE-specific syscalls ---- */

unsigned int getticks(void) {
    return (unsigned int)syscall0(400);
}

/* Raw SiMPLE stat syscall (409): fills {size, is_dir, exists}. */
typedef struct {
    unsigned int  size;
    unsigned char is_dir;
    unsigned char exists;
} __simple_stat_t;

static int __stat_simple(const char *path, __simple_stat_t *out) {
    long ret = syscall2(409, (long)path, (long)out);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int readdir_path(const char *path, void *buf, int max_entries) {
    long ret = syscall3(410, (long)path, (long)buf, max_entries);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* POSIX struct stat — layout must match user/include/sys/stat.h */
struct __posix_stat {
    unsigned long st_dev;
    unsigned int  st_ino;
    unsigned int  st_mode;
    unsigned      st_nlink;
    unsigned int  st_uid;
    unsigned int  st_gid;
    unsigned long st_rdev;
    int           st_size;
    unsigned      st_blksize;
    unsigned      st_blocks;
    long          st_atime;
    long          st_mtime;
    long          st_ctime;
};

#define __S_IFREG 0100000
#define __S_IFDIR 0040000
#define __S_IFCHR 0020000

static void __stat_fill(struct __posix_stat *st, unsigned size, int is_dir) {
    st->st_dev   = 0;
    st->st_ino   = 0;
    st->st_mode  = is_dir ? (__S_IFDIR | 0755) : (__S_IFREG | 0644);
    st->st_nlink = 1;
    st->st_uid   = 0;
    st->st_gid   = 0;
    st->st_rdev  = 0;
    st->st_size  = (int)size;
    st->st_blksize = 512;
    st->st_blocks  = (size + 511) / 512;
    /* FAT16 timestamps are not read by the kernel.  Use a constant
     * nonzero time: editors distinguish "file exists" (mtime > 0)
     * from "no backing file" (mtime <= 0), and a constant value means
     * "never externally modified", which is true on a single-tasking
     * filesystem. */
    st->st_atime = st->st_mtime = st->st_ctime = 1;
}

int stat(const char *path, struct __posix_stat *st) {
    __simple_stat_t ss;
    if (__stat_simple(path, &ss) < 0) return -1;
    if (!ss.exists) { errno = 2 /* ENOENT */; return -1; }
    __stat_fill(st, ss.size, ss.is_dir);
    return 0;
}

int lstat(const char *path, struct __posix_stat *st) {
    return stat(path, st);   /* no symlinks on FAT16 */
}

/* fstat — TTYs report a character device; file fds recover their size
 * via lseek (saving and restoring the current offset). */
int fstat(int fd, struct __posix_stat *st) {
    if (isatty(fd)) {
        __stat_fill(st, 0, 0);
        st->st_mode = __S_IFCHR | 0620;
        return 0;
    }
    int cur = lseek(fd, 0, 1 /* SEEK_CUR */);
    if (cur < 0) { errno = 9 /* EBADF */; return -1; }
    int end = lseek(fd, 0, 2 /* SEEK_END */);
    lseek(fd, cur, 0 /* SEEK_SET */);
    __stat_fill(st, end >= 0 ? (unsigned)end : 0, 0);
    return 0;
}

/* access — check file existence via stat */
int access(const char *path, int mode) {
    (void)mode;
    __simple_stat_t st;
    if (__stat_simple(path, &st) < 0) return -1;
    if (!st.exists) { errno = 2; return -1; }
    return 0;
}

/* getdents / getdents64 */
int getdents(int fd, void *buf, int count) {
    long ret = syscall3(141, fd, (long)buf, count);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

int getdents64(int fd, void *buf, int count) {
    long ret = syscall3(220, fd, (long)buf, count);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

struct pollfd {
    int   fd;
    short events;
    short revents;
};

int poll(struct pollfd *fds, int nfds, int timeout) {
    long ret = syscall3(168, (long)fds, nfds, timeout);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* uname */
int uname(void *buf) {
    long ret = syscall1(122, (long)buf);
    if (ret < 0) { errno = (int)-ret; return -1; }
    return (int)ret;
}

/* select — stub */
int select(int n, void *rfds, void *wfds, void *efds, void *timeout) {
    (void)n; (void)rfds; (void)wfds; (void)efds; (void)timeout;
    errno = 38; /* ENOSYS */
    return -1;
}

/* ---- Window Manager syscalls (401-407 range) ---- */

int wm_create(int x, int y, int w, int h) {
    long ret = syscall3(401, x, y, (w << 16) | (h & 0xffff));
    return (int)ret;
}

int wm_destroy(int wid) {
    long ret = syscall1(402, wid);
    return (int)ret;
}

int wm_blit(int wid, unsigned int *buf, int len) {
    long ret = syscall3(403, wid, (long)buf, len);
    return (int)ret;
}

int wm_move(int wid, int x, int y) {
    long ret = syscall3(404, wid, x, y);
    return (int)ret;
}

int wm_event(void *ev, int max) {
    long ret = syscall2(405, (long)ev, max);
    return (int)ret;
}

int wm_flush(int wid) {
    long ret = syscall1(406, wid);
    return (int)ret;
}

int wm_setfocus(int wid) {
    long ret = syscall1(407, wid);
    return (int)ret;
}

void sys_powerctl(int mode) {
    syscall1(408, mode);
}

/* Legacy sbrk that returns int (old ABI) */
int sbrk_inc(int increment) {
    return (int)(long)sbrk(increment);
}

/* stat_simple / readdir_simple — direct aliases */
int stat_simple(const char *path, void *out) {
    return stat(path, out);
}

int readdir_simple(const char *path, void *buf, int max) {
    return readdir_path(path, buf, max);
}
