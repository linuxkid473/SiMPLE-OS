/* user/systest.c */

#include <stdint.h>

/* libc */
int write(const char* buf, int len);
void exit(void);
int open(const char* path, int flags);
int close(int fd);
int fd_read(int fd, void* buf, int len);
int fd_write(int fd, const void* buf, int len);

/* flags */
#define O_READ   (1 << 0)
#define O_WRITE  (1 << 1)
#define O_CREATE (1 << 2)

static int strlen(const char* s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void puts(const char* s) {
    write(s, strlen(s));
}

static int strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }

    return *a - *b;
}

void _start(void) {
    int fd;
    int n;
    char buf[64];
    const char* msg = "hello from userspace";
    int msg_len = 21;

    puts("SYS_FWRITE test\n");

    fd = open("write.txt", O_WRITE | O_CREATE);

    if (fd < 0) {
        puts("open failed\n");
        exit();
    }

    puts("opened write.txt\n");

    n = fd_write(fd, msg, msg_len);

    if (n < 0) {
        puts("fd_write failed\n");
        close(fd);
        exit();
    }

    puts("wrote 21 bytes\n");

    close(fd);

    fd = open("write.txt", O_READ);

    if (fd < 0) {
        puts("reopen failed\n");
        exit();
    }

    puts("reopened file\n");

    n = fd_read(fd, buf, sizeof(buf) - 1);

    if (n < 0) {
        puts("fd_read failed\n");
        close(fd);
        exit();
    }

    buf[n] = '\0';

    puts("readback: ");
    puts(buf);
    puts("\n");

    if (strcmp(buf, msg) == 0)
        puts("verification success\n");
    else
        puts("verification FAILED\n");

    close(fd);

    puts("test complete\n");

    exit();
}