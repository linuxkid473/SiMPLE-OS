/* user/seektest.c — SYS_SEEK integration test */

#include <stdint.h>

/* libc */
int  write(const char* buf, int len);
void exit(int code);
int  open(const char* path, int flags);
int  close(int fd);
int  fd_read(int fd, void* buf, int len);
int  fd_write(int fd, const void* buf, int len);
int  seek(int fd, int offset, int whence);

/* open flags */
#define O_READ   (1 << 0)
#define O_WRITE  (1 << 1)
#define O_CREATE (1 << 2)

/* seek whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* -------------------------------------------------- */
/* helpers                                            */
/* -------------------------------------------------- */

static int mystrlen(const char* s) {
    int n = 0;

    while (s[n])
        n++;

    return n;
}

static void puts(const char* s) {
    write(s, mystrlen(s));
}

static void put_num(int n) {
    char buf[16];
    int i = 0;
    int lo, hi;
    char tmp;

    if (n == 0) {
        puts("0");
        return;
    }

    if (n < 0) {
        puts("-");
        n = -n;
    }

    while (n > 0) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }

    lo = 0;
    hi = i - 1;

    while (lo < hi) {
        tmp = buf[lo];
        buf[lo] = buf[hi];
        buf[hi] = tmp;
        lo++;
        hi--;
    }

    buf[i] = '\0';
    puts(buf);
}

static void put_hex_byte(unsigned char b) {
    const char* hex = "0123456789abcdef";
    char tmp[3];

    tmp[0] = hex[(b >> 4) & 0xF];
    tmp[1] = hex[b & 0xF];
    tmp[2] = '\0';

    puts(tmp);
}

/* -------------------------------------------------- */
/* entry                                              */
/* -------------------------------------------------- */

void _start(void) {
    const char* fname = "seek.txt";

    char rbuf[32];
    char tmp[8];

    int fd;
    int n;
    int pos;
    int ok;

    int i;

    ok = 1;

    puts("SYS_SEEK test\n");

    /* open/create file */
    fd = open(fname, O_WRITE | O_CREATE);

    if (fd < 0) {
        puts("open failed\n");
        exit(1);
    }

    puts("opened seek.txt\n");

    /* write AAAA at offset 0 */
    n = fd_write(fd, "AAAA", 4);

    if (n < 0) {
        puts("write AAAA failed\n");
        close(fd);
        exit(1);
    }

    puts("write AAAA\n");

    /* seek to offset 8 */
    pos = seek(fd, 8, SEEK_SET);

    if (pos < 0) {
        puts("seek failed\n");
        close(fd);
        exit(1);
    }

    puts("seek -> ");
    put_num(pos);
    puts("\n");

    /* write BBBB */
    n = fd_write(fd, "BBBB", 4);

    if (n < 0) {
        puts("write BBBB failed\n");
        close(fd);
        exit(1);
    }

    puts("write BBBB\n");

    close(fd);

    /* reopen read-only */
    fd = open(fname, O_READ);

    if (fd < 0) {
        puts("reopen failed\n");
        exit(1);
    }

    puts("reopened file\n");

    /* read file */
    n = fd_read(fd, rbuf, sizeof(rbuf) - 1);

    if (n < 0) {
        puts("fd_read failed\n");
        close(fd);
        exit(1);
    }

    rbuf[n] = '\0';

    /* dump bytes */
    puts("hex dump:");

    for (i = 0; i < n; i++) {
        puts(" ");
        put_hex_byte((unsigned char)rbuf[i]);
    }

    puts("\n");

    /* verify layout */
    if (n != 12) {
        puts("length FAILED\n");
        ok = 0;
    }

    for (i = 0; i < 4; i++) {
        if (rbuf[i] != 'A')
            ok = 0;
    }

    for (i = 4; i < 8; i++) {
        if (rbuf[i] != 0)
            ok = 0;
    }

    for (i = 8; i < 12; i++) {
        if (rbuf[i] != 'B')
            ok = 0;
    }

    if (ok)
        puts("verification success\n");
    else
        puts("verification FAILED\n");

    /* SEEK_CUR test */
    pos = seek(fd, 0, SEEK_SET);

    if (pos != 0) {
        puts("SEEK_SET failed\n");
        ok = 0;
    }

    n = fd_read(fd, tmp, 4);

    if (n != 4) {
        puts("SEEK_CUR prep read failed\n");
        ok = 0;
    }

    pos = seek(fd, 4, SEEK_CUR);

    if (pos != 8) {
        puts("SEEK_CUR failed\n");
        ok = 0;
    } else {
        puts("SEEK_CUR ok\n");
    }

    /* SEEK_END test */
    pos = seek(fd, 0, SEEK_END);

    if (pos != 12) {
        puts("SEEK_END failed\n");
        ok = 0;
    } else {
        puts("SEEK_END ok\n");
    }

    /* invalid negative seek */
    pos = seek(fd, -100, SEEK_SET);

    if (pos >= 0) {
        puts("negative seek should fail\n");
        ok = 0;
    } else {
        puts("negative seek rejected ok\n");
    }

    /* invalid whence */
    pos = seek(fd, 0, 99);

    if (pos >= 0) {
        puts("invalid whence should fail\n");
        ok = 0;
    } else {
        puts("invalid whence rejected ok\n");
    }

    close(fd);

    puts("test complete\n");

    if (ok)
        exit(0);
    else
        exit(1);
}