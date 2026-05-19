static int write(const char *buf, int len) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(1), "c"(buf), "d"(len)
        : "memory"
    );

    return ret;
}

static void yield(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(4)
        : "memory"
    );
}

static void exit(int code) {
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

void _start(void) {
    write("yieldtest start\n", 16);

    for (int i = 0; i < 5; i++) {
        write("calling SYS_YIELD\n", 20);

        yield();

        write("returned from SYS_YIELD\n", 26);
    }

    write("yieldtest done\n", 15);

    exit(0);
}