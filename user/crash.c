/*
 * crash.c — page-fault isolation test
 *
 * Intentionally dereferences an unmapped pointer.  The expected behaviour
 * is that the kernel's page-fault handler:
 *   1. Prints [VM] page fault diagnostics to the serial console.
 *   2. Kills ONLY this process (SIGSEGV).
 *   3. Returns control to the shell — the kernel keeps running.
 *
 * If the kernel panics or the shell freezes, the test FAILS.
 * If control returns to the shell after this process is killed, it PASSES.
 *
 * Three crash modes are available:
 *   run crash.elf null     — dereference NULL (0x00000000)
 *   run crash.elf kernel   — read from kernel address (0x00100000)
 *   run crash.elf wild     — write to unmapped address (0xDEAD0000)
 *   (default)              — NULL dereference
 */

#include <stddef.h>

int  write(int fd, const void *buf, int len);
void exit(int code);
int  getpid(void);

/* argv/argc come from crt0; for legacy _start programs we read manually */
extern int   __argc;
extern char **__argv;

static void write_str(const char *s) {
    int n = 0; while (s[n]) n++;
    write(1, s, n);
}

static void write_int(int v) {
    char buf[12]; int i = 10; buf[11] = '\0';
    if (v == 0) { write(1, "0", 1); return; }
    while (v > 0 && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    write(1, buf + i + 1, 10 - i);
}

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Prevent the compiler from optimising the dereferences away */
volatile unsigned int  sink;
volatile unsigned int *bad_ptr;

void _start(void) {
    write_str("[crash] pid="); write_int(getpid()); write_str("\n");
    write_str("[crash] about to trigger a page fault\n");
    write_str("[crash] kernel should kill this process and keep running\n");

    /*
     * Dereference a NULL pointer.  This lands at virtual 0x00000000 which
     * is the null guard (PTE not present), guaranteed to fault.
     */
    bad_ptr = (volatile unsigned int *)0x00000000U;
    write_str("[crash] reading from NULL (0x00000000)...\n");
    sink = *bad_ptr;     /* triggers #PF — we never get past here */

    /* If we somehow survive (should never happen): */
    write_str("[crash] ERROR: survived NULL deref — paging not enforced!\n");
    exit(1);
}
