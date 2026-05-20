/*
 * hog.c — CPU hog that never voluntarily yields.
 * Proves the PIT timer preempts ring3 processes without any yield() call.
 *
 * Usage: run hog.elf
 * Expected: despite the infinite spin, the shell remains responsive
 *           and other processes (if any) continue executing.
 */

int  write(const char *buf, int len);
void exit(int code);

static void write_str(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

static void write_uint(unsigned long v) {
    char buf[21];
    int  i = 19;
    buf[20] = '\0';
    if (v == 0) { write("0", 1); return; }
    while (v && i >= 0) { buf[i--] = '0' + (int)(v % 10); v /= 10; }
    const char *p = buf + i + 1;
    int len = 0;
    while (p[len]) len++;
    write(p, len);
}

void _start(void) {
    write_str("[hog] spinning — no yield() calls\n");

    volatile unsigned long counter = 0;

    for (;;) {
        counter++;

        /* Print a progress message every ~50M iterations.
         * If preemption works, you will see these interleaved with output
         * from other processes (or the shell prompt will remain usable). */
        if ((counter % 50000000UL) == 0) {
            write_str("[hog] count=");
            write_uint(counter);
            write_str("\n");

            /* Stop after 5 prints so the shell eventually gets control back */
            if (counter >= 250000000UL)
                break;
        }
    }

    write_str("[hog] done\n");
    exit(0);
}
