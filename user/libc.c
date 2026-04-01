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

void exit(void) {
    __asm__ volatile(
        "int $0x80"
        :
        : "a"(2)
    );
}
