/* user/opentest.c
 *
 * Simple SYS_OPEN test program for SiMPLE OS.
 *
 * Tests:
 *  - open existing file
 *  - create missing file
 *  - invalid open handling
 *  - errno-style returns
 */

#include <stdint.h>

/* syscall numbers */
#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_OPEN  5

/* open flags */
#define O_READ   (1 << 0)
#define O_WRITE  (1 << 1)
#define O_CREATE (1 << 2)

/* errno values */
#define ENOENT  2
#define EINVAL 22
#define EMFILE 24

static inline int write(const char* buf, uint32_t len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE),
          "c"(buf),
          "d"(len)
        : "memory"
    );
    return ret;
}

static inline void exit(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(SYS_EXIT)
        : "memory"
    );

    for (;;);
}

static inline int open(const char* path, int flags) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_OPEN),
          "c"(path),
          "d"(flags)
        : "memory"
    );

    return ret;
}

static uint32_t strlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void puts(const char* s) {
    write(s, strlen(s));
}

static void print_result(const char* name, int fd) {
    puts(name);

    if (fd >= 0) {
        puts(" -> success [fd allocated]\n");
        return;
    }

    puts(" -> failed: ");

    switch (-fd) {
        case ENOENT:
            puts("ENOENT");
            break;

        case EINVAL:
            puts("EINVAL");
            break;

        case EMFILE:
            puts("EMFILE");
            break;

        default:
            puts("unknown");
            break;
    }

    puts("\n");
}

void _start(void) {
    int fd;

    puts("=== SYS_OPEN TEST ===\n\n");

    /* existing file test */
    fd = open("hello.elf", O_READ);
    print_result("open hello.elf O_READ", fd);

    /* missing file test */
    fd = open("missing.txt", O_READ);
    print_result("open missing.txt O_READ", fd);

    /* create test */
    fd = open("created.txt", O_WRITE | O_CREATE);
    print_result("open created.txt O_WRITE|O_CREATE", fd);

    /* invalid flags test */
    fd = open("bad.txt", 0);
    print_result("open bad.txt flags=0", fd);

    puts("\nSYS_OPEN test complete.\n");

    exit();
}