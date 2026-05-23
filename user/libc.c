// user/libc.c
#include "wm.h"

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

/* wait — block until any child exits; returns child's exit code, or -1. */
int wait(void) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(12)
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

/* sbrk — grow heap by increment bytes.
 * Returns old break (pointer to start of new region), or -1 on failure. */
int sbrk(int increment) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(15), "c"(increment)
        : "memory"
    );
    return ret;
}

/* -----------------------------------------------------------------------
 * Window Manager syscall wrappers (syscalls 20-26)
 *
 * SYS_WM_CREATE packs w and h into ebx as (w<<16 | h) so all four
 * geometry parameters fit in the standard 3-register ABI.
 * ----------------------------------------------------------------------- */

/* Create a window at (x,y) with dimensions w×h.
 * Returns wid (>= 0) or negative errno on failure. */
int wm_create(int x, int y, int w, int h) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(20), "c"(x), "d"(y), "b"((w << 16) | (h & 0xffff))
        : "memory"
    );
    return ret;
}

/* Destroy a window.  Returns 0 or -EBADF. */
int wm_destroy(int wid) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(21), "c"(wid)
        : "memory"
    );
    return ret;
}

/* Blit a 32bpp pixel buffer (w*h*4 bytes) into the window.
 * len must be >= w*h*4.  Returns 0 or negative errno. */
int wm_blit(int wid, unsigned int *buf, int len) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(22), "c"(wid), "d"(buf), "b"(len)
        : "memory"
    );
    return ret;
}

/* Move a window to (x,y); re-blits the last pixel buffer.
 * Returns 0 or -EBADF. */
int wm_move(int wid, int x, int y) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(23), "c"(wid), "d"(x), "b"(y)
        : "memory"
    );
    return ret;
}

/* Dequeue one event into *ev.  Non-blocking.
 * Returns event type (> 0) or 0 if queue empty. */
int wm_event(wm_event_t *ev, int max) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(24), "c"(ev), "d"(max)
        : "memory"
    );
    return ret;
}

/* Flush wid to framebuffer (-1 = all windows).  Returns 0. */
int wm_flush(int wid) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(25), "c"(wid)
        : "memory"
    );
    return ret;
}

/* Set the focused user window (keyboard events route to it).
 * Returns 0 or -EBADF. */
int wm_setfocus(int wid) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(26), "c"(wid)
        : "memory"
    );
    return ret;
}