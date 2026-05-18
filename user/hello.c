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

static int sys_read(char *buf, int max_len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(3), "c"(buf), "d"(max_len)
        : "memory"
    );
    return ret;
}

static void sys_exit(int code) {
    (void)code;

    __asm__ volatile(
        "int $0x80"
        :
        : "a"(2)
        : "memory"
    );

    while (1)
        __asm__ volatile("hlt");
}

static int strlen(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

void _start(void) {
    char buf[128];

    sys_write("=== readtest ===\n", 19);
    sys_write("Type something: ", 16);

    int n = sys_read(buf, sizeof(buf));

    sys_write("\nYou typed: ", 13);
    sys_write(buf, n);
    sys_write("\n", 1);

    sys_exit(0);
}