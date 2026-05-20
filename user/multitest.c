/*
 * multitest.c — preemptive multitasking demonstration.
 *
 * The parent forks two children.  All three processes then spin in a
 * counting loop with NO yield() calls.  If the PIT timer is working,
 * output from all three will interleave on the screen, proving that
 * preemption is real and not just cooperative scheduling.
 *
 * Expected output (interleaved, order may vary):
 *   [parent] lap N
 *   [child1] lap N
 *   [child2] lap N
 *   ...
 */

int  fork(void);
int  write(const char *buf, int len);
void exit(int code);

static void write_str(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

static void write_int(int v) {
    char buf[12];
    int  i = 10;
    buf[11] = '\0';
    if (v == 0) { write("0", 1); return; }
    while (v && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    const char *p = buf + i + 1;
    int len = 0;
    while (p[len]) len++;
    write(p, len);
}

/* Spin for roughly N*1M iterations (so progress is visible at 100 Hz) */
static void spin(volatile unsigned long iters) {
    while (iters--) {}
}

void _start(void) {
    int pid1 = fork();
    if (pid1 < 0) {
        write_str("[multi] fork1 failed\n");
        exit(1);
    }

    if (pid1 == 0) {
        /* child 1 — no yield, purely preemptive */
        for (int lap = 1; lap <= 8; lap++) {
            write_str("[child1] lap ");
            write_int(lap);
            write_str("\n");
            spin(20000000UL);
        }
        write_str("[child1] done\n");
        exit(0);
    }

    int pid2 = fork();
    if (pid2 < 0) {
        write_str("[multi] fork2 failed\n");
        exit(1);
    }

    if (pid2 == 0) {
        /* child 2 — no yield, purely preemptive */
        for (int lap = 1; lap <= 8; lap++) {
            write_str("[child2] lap ");
            write_int(lap);
            write_str("\n");
            spin(20000000UL);
        }
        write_str("[child2] done\n");
        exit(0);
    }

    /* parent — no yield, purely preemptive */
    for (int lap = 1; lap <= 8; lap++) {
        write_str("[parent] lap ");
        write_int(lap);
        write_str("\n");
        spin(20000000UL);
    }
    write_str("[parent] done\n");
    exit(0);
}
