/*
 * isoB.c — isolation_test_b
 *
 * Counterpart to isoA.c.  Uses pattern 0xBBBBBBBB on the SAME virtual
 * addresses as isoA.  Each process has its own physical pages backing those
 * addresses, so the patterns never interfere.
 *
 * Expected outcome: both isoA and isoB print PASS.
 */

#include <stddef.h>

int  write(int fd, const void *buf, int len);
void exit(int code);
int  nanosleep(const void *req, void *rem);
int  getpid(void);

#define BUF_LEN   256
#define PATTERN_B 0xBBBBBBBBU

static unsigned int data[BUF_LEN];

static void write_str(const char *s) {
    int n = 0;
    while (s[n]) n++;
    write(1, s, n);
}

static void write_hex(unsigned int v) {
    char buf[11];
    buf[0]  = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        int nibble = v & 0xF;
        buf[i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        v >>= 4;
    }
    buf[10] = '\0';
    write(1, buf, 10);
}

static void write_int(int v) {
    char buf[12];
    int  i = 10;
    buf[11] = '\0';
    if (v == 0) { write(1, "0", 1); return; }
    while (v > 0 && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    write(1, buf + i + 1, 10 - i);
}

static void msleep(int ms) {
    struct { int tv_sec; int tv_nsec; } req;
    req.tv_sec  = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&req, (void *)0);
}

void _start(void) {
    int pid = getpid();

    write_str("[isoB] pid="); write_int(pid);
    write_str(" writing pattern 0xBBBBBBBB to ");
    write_int(BUF_LEN);
    write_str(" words\n");

    for (int i = 0; i < BUF_LEN; i++)
        data[i] = PATTERN_B;

    write_str("[isoB] sleeping 3s to let isoA run concurrently\n");
    msleep(3000);

    int ok = 1;
    for (int i = 0; i < BUF_LEN; i++) {
        if (data[i] != PATTERN_B) {
            write_str("[isoB] FAIL: data[");
            write_int(i);
            write_str("] = ");
            write_hex(data[i]);
            write_str(" expected ");
            write_hex(PATTERN_B);
            write_str("\n");
            ok = 0;
            break;
        }
    }

    if (ok)
        write_str("[isoB] PASS: 256-word buffer intact after concurrent run\n");
    else
        write_str("[isoB] FAIL: buffer was corrupted (isolation broken!)\n");

    exit(ok ? 0 : 1);
}
