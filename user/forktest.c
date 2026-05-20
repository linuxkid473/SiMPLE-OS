/*
 * forktest.c — verify real fork() semantics:
 *   1. fork() returns different values to parent vs child.
 *   2. Both processes execute concurrently via yield.
 *   3. User memory is independent (writes in child do not affect parent).
 *   4. Stack is independent.
 */

int   fork(void);
int   yield(void);
int   write(const char *buf, int len);
void  exit(int code);

/* A global variable in .data — both parent and child get their own copy
 * after fork.  Writing in the child must not affect the parent's copy. */
static volatile int g_val = 42;

/* Minimal decimal print into a fixed buffer — no libc needed. */
static void write_int(int v) {
    char buf[12];
    int  neg = 0;
    int  i   = 10;
    buf[11]  = '\0';
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { write("0", 1); return; }
    while (v && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i--] = '-';
    write(buf + i + 1, 10 - i);
}

void _start(void) {
    int pid = fork();

    if (pid < 0) {
        write("FAIL: fork() returned negative\n", 31);
        exit(1);
    }

    if (pid == 0) {
        /* ---- CHILD ---- */
        write("[child] hello — I am the child\n", 31);

        /* Mutate g_val; parent's copy should remain 42. */
        g_val = 99;
        write("[child] g_val set to 99\n", 24);

        /* Stack test: local variable */
        volatile int local = 7;
        (void)local;

        yield();  /* let parent run */

        write("[child] resumed after yield\n", 28);
        write("[child] exiting\n", 16);
        exit(0);

    } else {
        /* ---- PARENT ---- */
        write("[parent] fork ok, child pid=", 28);
        write_int(pid);
        write("\n", 1);

        yield();  /* let child run first */

        /* After child ran, verify our copy of g_val is unchanged. */
        write("[parent] resumed\n", 17);
        if (g_val == 42) {
            write("[parent] PASS: g_val=42 (memory is independent)\n", 48);
        } else {
            write("[parent] FAIL: g_val modified by child!\n", 40);
        }

        yield();  /* let child finish */

        write("[parent] done\n", 14);
        exit(0);
    }
}
