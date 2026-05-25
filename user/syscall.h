/*
 * user/syscall.h — user-space syscall wrappers and shared struct types.
 *
 * All functions use the int 0x80 ABI:
 *   eax = syscall number
 *   ecx = arg0, edx = arg1, ebx = arg2
 *   return value in eax
 *
 * Struct layouts here MUST match the kernel-side definitions in syscall.c.
 */
#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

/* Syscall numbers */
#define SYS_WRITE    1
#define SYS_EXIT     2
#define SYS_READ     3
#define SYS_YIELD    4
#define SYS_OPEN     5
#define SYS_CLOSE    6
#define SYS_FREAD    7
#define SYS_FWRITE   8
#define SYS_SEEK     9
#define SYS_EXEC     10
#define SYS_FORK     11
#define SYS_WAIT     12
#define SYS_GETPID   13
#define SYS_SLEEP    14
#define SYS_SBRK     15
#define SYS_STAT     16
#define SYS_READDIR  17
#define SYS_RENAME   18
#define SYS_GETTICKS 19

/* Result type for SYS_STAT.
 * Filled by the kernel; exists=0 means the path was not found. */
typedef struct {
    unsigned int size;
    unsigned char is_dir;
    unsigned char exists;
} stat_t;

/* Per-entry type for SYS_READDIR.
 * name is a null-terminated 8.3 filename. */
typedef struct {
    char name[64];
    unsigned char is_dir;
    unsigned char _pad[3]; /* explicit padding to match kernel layout */
    unsigned int  size;
} dirent_t;

/* -----------------------------------------------------------------------
 * Inline wrappers
 * ----------------------------------------------------------------------- */

static inline int getpid(void) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETPID)
    );
    return ret;
}

/* Sleep for `ticks` PIT ticks (PIT runs at 100 Hz → 100 ticks ≈ 1 second). */
static inline int sys_sleep(unsigned int ticks) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SLEEP), "c"(ticks)
        : "memory"
    );
    return ret;
}

/* Return the global PIT tick counter. */
static inline unsigned int getticks(void) {
    unsigned int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETTICKS)
    );
    return ret;
}

/* Fill *out with metadata for the file/directory at path.
 * Returns 0 on success, -1 if not found. */
static inline int stat(const char *path, stat_t *out) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_STAT), "c"(path), "d"(out)
        : "memory"
    );
    return ret;
}

/* Read up to max_entries directory entries from path into buf[].
 * Returns number of entries written, or -1 on error. */
static inline int readdir(const char *path, dirent_t *buf, int max_entries) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_READDIR), "c"(path), "d"(buf), "b"(max_entries)
        : "memory"
    );
    return ret;
}

/* Rename old_path to new_path (both root-relative).
 * Returns 0 on success, -1 on error. */
static inline int rename(const char *old_path, const char *new_path) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_RENAME), "c"(old_path), "d"(new_path)
        : "memory"
    );
    return ret;
}

#endif /* USER_SYSCALL_H */
