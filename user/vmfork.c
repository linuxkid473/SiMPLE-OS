/*
 * vmfork.c — fork address-space isolation test
 *
 * Verifies that after fork():
 *   (a) The child gets a copy-on-write / independent copy of all memory.
 *   (b) Modifications made by the child are NOT visible to the parent.
 *   (c) The parent's memory remains unchanged.
 *
 * Test layout:
 *   1. Parent sets global array to BEFORE_FORK value.
 *   2. Parent forks.
 *   3. Child overwrites the array with AFTER_FORK value and exits.
 *   4. Parent waits for child, then checks: array must still hold BEFORE_FORK.
 *
 * A PASS result confirms per-process address-space isolation.
 * A FAIL result means child writes leaked back into the parent's pages.
 */

#include <stddef.h>

int  fork(void);
int  waitpid(int pid, int *status, int options);
int  write(int fd, const void *buf, int len);
void exit(int code);
int  getpid(void);

#define BUF_LEN     128
#define BEFORE_FORK 0x11223344U
#define AFTER_FORK  0xDEADBEEFU

static unsigned int shared_buf[BUF_LEN];

static void write_str(const char *s) {
    int n = 0; while (s[n]) n++;
    write(1, s, n);
}

static void write_int(int v) {
    char buf[12]; int i = 10; buf[11] = '\0';
    if (v == 0) { write(1, "0", 1); return; }
    int neg = (v < 0); if (neg) v = -v;
    while (v > 0 && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i--] = '-';
    write(1, buf + i + 1, 10 - i);
}

static void write_hex(unsigned int v) {
    char buf[11]; buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        int n = v & 0xF;
        buf[i] = (n < 10) ? ('0' + n) : ('a' + n - 10);
        v >>= 4;
    }
    buf[10] = '\0';
    write(1, buf, 10);
}

void _start(void) {
    write_str("[vmfork] parent pid="); write_int(getpid()); write_str("\n");

    /* Step 1: fill buffer with BEFORE_FORK pattern */
    for (int i = 0; i < BUF_LEN; i++)
        shared_buf[i] = BEFORE_FORK;

    write_str("[vmfork] buffer filled with "); write_hex(BEFORE_FORK);
    write_str(", forking\n");

    int child_pid = fork();

    if (child_pid < 0) {
        write_str("[vmfork] FAIL: fork() returned "); write_int(child_pid); write_str("\n");
        exit(1);
    }

    if (child_pid == 0) {
        /* ---- CHILD ---- */
        write_str("[vmfork] child pid="); write_int(getpid());
        write_str(" overwriting buffer with "); write_hex(AFTER_FORK); write_str("\n");

        for (int i = 0; i < BUF_LEN; i++)
            shared_buf[i] = AFTER_FORK;

        write_str("[vmfork] child done, exiting\n");
        exit(0);
    }

    /* ---- PARENT ---- */
    write_str("[vmfork] parent waiting for child pid="); write_int(child_pid);
    write_str("\n");

    int status = 0;
    waitpid(child_pid, &status, 0);

    write_str("[vmfork] parent checking buffer\n");

    int ok = 1;
    for (int i = 0; i < BUF_LEN; i++) {
        if (shared_buf[i] != BEFORE_FORK) {
            write_str("[vmfork] FAIL: buf["); write_int(i); write_str("] = ");
            write_hex(shared_buf[i]); write_str(" (expected "); write_hex(BEFORE_FORK);
            write_str(") — child write leaked into parent!\n");
            ok = 0;
            break;
        }
    }

    if (ok)
        write_str("[vmfork] PASS: parent buffer unchanged after child overwrite\n");

    exit(ok ? 0 : 1);
}
