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

int fd_read(int fd, void* buf, int len) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(7), "c"(fd), "d"(buf), "b"(len)
        : "memory"
    );

    return ret;
}

int fd_write(int fd, const void* buf, int len) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(8), "c"(fd), "d"(buf), "b"(len)
        : "memory"
    );

    return ret;
}

int seek(int fd, int offset, int whence) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(9), "c"(fd), "d"(offset), "b"(whence)
        : "memory"
    );

    return ret;
}

/* exec — replace the current process image with the ELF at path.
 * Does not return on success.  Returns -errno on failure. */
int exec(const char *path) {
    int ret;

    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(10), "c"(path)
        : "memory"
    );

    return ret;
}

/*
 * fork — duplicate the current process.
 *   Parent receives the child's pid (>0).
 *   Child  receives 0.
 *   Returns -1 on failure.
 *
 * The child starts executing at the instruction immediately after the
 * int $0x80 (i.e. here, returning from the inline asm), with an
 * independent copy of all user memory and the register state.
 */
int fork(void) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(11)
        : "memory"
    );
    return ret;
}

/* yield — cooperatively hand CPU to the next runnable process. */
int yield(void) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(4)
        : "memory"
    );
    return ret;
}