// user/libc.c

int write(const char* buf, int len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(1), "c"(buf), "d"(len)
    );
    return ret;
}

void exit(int code) {
    (void)code;

    __asm__ volatile(
        "int $0x80"
        :
        : "a"(2)
    );

    for (;;);
}

int open(const char* path, int flags) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(5), "c"(path), "d"(flags)
        : "memory"
    );

    return ret;
}

int close(int fd) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(6), "c"(fd)
        : "memory"
    );

    return ret;
}