/* user/systest.c */

#include <stdint.h>

/* libc wrappers */
int write(const char* buf, int len);
void exit(void);
int open(const char* path, int flags);
int close(int fd);
int fd_read(int fd, void* buf, int len);

/* open flags */
#define O_READ   (1 << 0)

static int strlen(const char* s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void puts(const char* s) {
    write(s, strlen(s));
}

static void put_hex_byte(uint8_t b) {
    char out[3];
    const char* hex = "0123456789ABCDEF";

    out[0] = hex[(b >> 4) & 0xF];
    out[1] = hex[b & 0xF];
    out[2] = '\0';

    puts(out);
}

static void put_num(int n) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        puts("0");
        return;
    }

    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i--) {
        char c[2];
        c[0] = buf[i];
        c[1] = '\0';
        puts(c);
    }
}

void _start(void) {
    int fd;
    int n;
    uint8_t buf[16];
    int i;

    puts("SYS_FREAD test\n");

    fd = open("hello.elf", O_READ);

    if (fd < 0) {
        puts("open failed\n");
        exit();
    }

    puts("opened hello.elf\n");

    n = fd_read(fd, buf, sizeof(buf));

    if (n < 0) {
        puts("fd_read failed\n");
        close(fd);
        exit();
    }

    puts("read ");
    put_num(n);
    puts(" bytes\n");

    puts("first 16 bytes:\n");

    for (i = 0; i < n; i++) {
        put_hex_byte(buf[i]);
        puts(" ");
    }

    puts("\n");

    n = fd_read(fd, buf, 8);

    puts("second read returned ");
    put_num(n);
    puts(" bytes\n");

    close(fd);

    puts("test complete\n");

    exit();
}