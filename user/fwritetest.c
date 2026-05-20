/* user/fwritetest.c — SYS_FWRITE integration test */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* libc declarations                                                    */
/* ------------------------------------------------------------------ */
int  write(const char* buf, int len);
void exit(int code);
int  open(const char* path, int flags);
int  close(int fd);
int  fd_read(int fd, void* buf, int len);
int  fd_write(int fd, const void* buf, int len);

/* ------------------------------------------------------------------ */
/* open flags                                                           */
/* ------------------------------------------------------------------ */
#define O_READ   (1 << 0)
#define O_WRITE  (1 << 1)
#define O_CREATE (1 << 2)

/* ------------------------------------------------------------------ */
/* minimal helpers                                                      */
/* ------------------------------------------------------------------ */
static int mystrlen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void puts(const char* s) {
    write(s, mystrlen(s));
}

static void put_num(int n) {
    char buf[12];
    int  i = 0;

    if (n < 0) { puts("-"); n = -n; }
    if (n == 0) { puts("0"); return; }

    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    /* reverse */
    int lo = 0, hi = i - 1;
    while (lo < hi) {
        char tmp  = buf[lo];
        buf[lo]   = buf[hi];
        buf[hi]   = tmp;
        lo++; hi--;
    }
    buf[i] = '\0';
    puts(buf);
}

static int mystrcmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* test entry point                                                     */
/* ------------------------------------------------------------------ */
void _start(void) {
    const char* fname = "write.txt";
    const char* msg   = "hello from userspace";
    int         mlen  = mystrlen(msg);
    char        rbuf[64];
    int         fd, n;

    puts("SYS_FWRITE test\n");

    /* 1. open for write, create if absent */
    fd = open(fname, O_WRITE | O_CREATE);
    if (fd < 0) {
        puts("open failed\n");
        exit(1);
    }
    puts("opened write.txt\n");

    /* 2. write test message via SYS_FWRITE */
    n = fd_write(fd, msg, mlen);
    if (n < 0) {
        puts("fd_write failed\n");
        close(fd);
        exit(1);
    }
    puts("wrote ");
    put_num(n);
    puts(" bytes\n");

    /* 3. close */
    close(fd);

    /* 4. reopen read-only — offset must reset to 0 */
    fd = open(fname, O_READ);
    if (fd < 0) {
        puts("reopen failed\n");
        exit(1);
    }
    puts("reopened file\n");

    /* 5. read back */
    n = fd_read(fd, rbuf, (int)sizeof(rbuf) - 1);
    if (n < 0) {
        puts("fd_read failed\n");
        close(fd);
        exit(1);
    }
    rbuf[n] = '\0';

    puts("readback: ");
    puts(rbuf);
    puts("\n");

    /* 6. verify content matches */
    int ok = (n == mlen) && mystrcmp(rbuf, msg, mlen);
    if (ok)
        puts("verification success\n");
    else
        puts("verification FAILED\n");

    close(fd);
    puts("test complete\n");
    exit(ok ? 0 : 1);
}
