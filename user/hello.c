/* user/closetest.c */

#include <stdint.h>

/* libc */
int write(const char* buf, int len);
void exit(int code);
int open(const char* path, int flags);
int close(int fd);

/* open flags */
#define O_READ   (1 << 0)
#define O_WRITE  (1 << 1)
#define O_CREATE (1 << 2)

static void puts(const char* s) {
    int len = 0;

    while (s[len])
        len++;

    write(s, len);
}

static void write_num(int n) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        puts("0");
        return;
    }

    if (n < 0) {
        puts("-");
        n = -n;
    }

    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i--) {
        char c[2];
        c[0] = buf[i];
        c[1] = 0;
        write(c, 1);
    }
}

void _start(void) {
    int fd1, fd2;
    int rc;

    puts("SYS_CLOSE test\n");

    fd1 = open("hello.elf", O_READ);

    if (fd1 < 0) {
        puts("open failed\n");
        exit(1);
    }

    puts("open -> fd ");
    write_num(fd1);
    puts("\n");

    rc = close(fd1);

    if (rc < 0) {
        puts("close failed\n");
        exit(1);
    }

    puts("close(");
    write_num(fd1);
    puts(") success\n");

    fd2 = open("hello.elf", O_READ);

    if (fd2 < 0) {
        puts("reopen failed\n");
        exit(1);
    }

    puts("reopen -> fd ");
    write_num(fd2);
    puts("\n");

    if (fd1 == fd2)
        puts("fd reuse verified\n");
    else
        puts("fd reuse FAILED\n");

    close(fd2);

    puts("test complete\n");

    exit(0);
}