/*
 * spam.c — minimal write test, no libc.
 *
 * SYS_WRITE = 1: ecx = str ptr, edx = length
 * SYS_EXIT  = 2: ecx = exit code
 */
#include "syscall.h"

static inline void write_str(const char *s, int len) {
    __asm__ volatile(
        "int $0x80"
        : : "a"(SYS_WRITE), "c"(s), "d"(len) : "memory"
    );
}

static inline void _exit(int code) {
    __asm__ volatile(
        "int $0x80"
        : : "a"(SYS_EXIT), "c"(code) : "memory"
    );
    __builtin_unreachable();
}

void _start(void) {
    write_str("spam 1\n", 7);
    write_str("spam 2\n", 7);
    write_str("spam 3\n", 7);
    _exit(0);
}
