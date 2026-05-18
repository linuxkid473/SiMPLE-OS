/*
 * user/systest.c — demonstration of the SiMPLE OS syscall ABI
 *
 * Syscall ABI (int 0x80):
 *   eax = number   ecx = arg0   edx = arg1   return → eax
 *
 *   1  SYS_WRITE  ecx=buf, edx=len  — write to active terminal
 *   2  SYS_EXIT                     — clean return to shell
 *
 * This file is self-contained: it does not use libc.c so the wrappers
 * are visible alongside the tests.
 */

/* ------------------------------------------------------------------ */
/* Inline syscall wrappers                                             */
/* ------------------------------------------------------------------ */

static int sys_write(const char *buf, int len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(1), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

static void sys_exit(int code) {
    (void)code;   /* exit code not yet used by the kernel — reserved for future */
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(2)
        : "memory"
    );
    /* If the kernel somehow falls through, spin safely */
    while (1) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* Tiny helpers                                                        */
/* ------------------------------------------------------------------ */

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *s) {
    sys_write(s, slen(s));
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void _start(void) {
    print("=== systest ===\n");
    print("SYS_WRITE (1): this line proves it works.\n");
    print("SYS_WRITE (1): so does this one.\n");
    print("Calling SYS_EXIT (2) — shell prompt should return cleanly.\n");
    sys_exit(0);
}
