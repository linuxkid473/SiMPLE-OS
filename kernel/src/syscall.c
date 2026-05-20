/*
 * syscall.c — kernel-side syscall implementations.
 *
 * Dispatched from idt.c's syscall_handler() on int 0x80.
 *
 * ABI (int 0x80):
 *   eax = syscall number
 *   ecx = arg0
 *   edx = arg1
 *   ebx = arg2   (3-argument syscalls only)
 *   return value in eax (set via regs->eax; restored by popa in ISR epilogue)
 *
 * Syscall table:
 *   1  SYS_WRITE  — write bytes to the active terminal
 *                   ecx = buf (const char*), edx = len
 *                   returns: bytes written
 *   2  SYS_EXIT   — terminate program and return to shell
 *                   (handled entirely in idt.c via exit_trampoline)
 *   3  SYS_READ   — blocking line input from the active terminal
 *                   ecx = buf (char*), edx = max_len (including NUL)
 *                   returns: bytes read (excluding NUL)
 *   4  SYS_YIELD  — cooperative yield (no-op; no scheduler in this kernel)
 *                   returns: 0
 *   5  SYS_OPEN   — open a file, allocate a file descriptor
 *                   ecx = path, edx = flags (O_READ|O_WRITE|O_CREATE)
 *                   returns: fd >= FD_MIN_USER, or negative errno
 *   6  SYS_CLOSE  — release a file descriptor
 *                   ecx = fd
 *                   returns: 0, or negative errno
 *   7  SYS_FREAD  — read bytes from an open fd
 *                   ecx = fd, edx = buf, ebx = max_len
 *                   returns: bytes read (0 = EOF), or negative errno
 *   8  SYS_FWRITE — write bytes to an open fd at current offset
 *                   ecx = fd, edx = buf, ebx = len
 *                   returns: bytes written, or negative errno
 */

#include "console.h"
#include "fat16.h"
#include "fd.h"
#include "klog.h"
#include "string.h"
#include "vga.h"
#include "types.h"

/*
 * Minimum valid userspace pointer.  Rejects NULL and the zero page.
 * In this ring-0 kernel user programs load at USER_BASE (0x100000) and
 * their stack sits just below 0x200000, so anything above 0x1000 that
 * doesn't wrap is a plausible user pointer.
 */
#define USER_ADDR_MIN 0x1000UL

static fat16_fs_t* g_fs          = NULL;
static fd_table_t  g_fd_table;
static int         g_fd_table_ok = 0;

void syscall_set_fs(fat16_fs_t* fs) {
    g_fs = fs;
    if (!g_fd_table_ok) {
        fd_table_init(&g_fd_table);
        g_fd_table_ok = 1;
    }
}

fd_table_t* syscall_get_fd_table(void) {
    if (!g_fd_table_ok) {
        fd_table_init(&g_fd_table);
        g_fd_table_ok = 1;
    }
    return &g_fd_table;
}

/* Returns 1 if [ptr, ptr+len) is a plausible userspace buffer. */
static int user_ptr_ok(const void* ptr, uint32_t len) {
    uint32_t addr = (uint32_t)ptr;
    if (!ptr)                         return 0;
    if (addr < USER_ADDR_MIN)         return 0;
    if (len > 0 && (addr + len) < addr) return 0;  /* wrap-around */
    return 1;
}

/* -----------------------------------------------------------------------
 * SYS_WRITE (1): ecx = buf, edx = len
 * ----------------------------------------------------------------------- */
int32_t sys_write(const char* buf, uint32_t len) {
    if (len == 0) return 0;
    if (len > 0x8000) len = 0x8000;
    if (!user_ptr_ok(buf, len)) {
        klog("syscall", "SYS_WRITE: invalid buf pointer");
        return -(int32_t)EINVAL;
    }
    for (uint32_t i = 0; i < len; i++)
        vga_putc(buf[i]);
    return (int32_t)len;
}

/* -----------------------------------------------------------------------
 * SYS_READ (3): ecx = buf, edx = max_len
 * ----------------------------------------------------------------------- */
int32_t sys_read(char* buf, uint32_t max_len) {
    if (max_len == 0) return 0;
    if (!user_ptr_ok(buf, max_len)) {
        klog("syscall", "SYS_READ: invalid buf pointer");
        return -(int32_t)EINVAL;
    }
    console_read_line(buf, max_len);
    return (int32_t)strlen(buf);
}

/* -----------------------------------------------------------------------
 * SYS_YIELD (4): cooperative yield — no-op in single-task kernel.
 * ----------------------------------------------------------------------- */
int32_t sys_yield(void) {
    return 0;
}

/* -----------------------------------------------------------------------
 * SYS_OPEN (5): ecx = path, edx = flags
 *
 * Accepts a root-relative 8.3 filename (leading '/' is stripped).
 * O_CREATE creates the file when it doesn't already exist.
 * ----------------------------------------------------------------------- */
int32_t sys_open(const char* path, uint32_t flags) {
    klog("syscall", "SYS_OPEN");

    if (!path || !user_ptr_ok(path, 1)) {
        klog("syscall", "SYS_OPEN: invalid path pointer");
        return -(int32_t)EINVAL;
    }
    if (!*path) {
        klog("syscall", "SYS_OPEN: empty path");
        return -(int32_t)EINVAL;
    }
    if (!(flags & (O_READ | O_WRITE))) {
        klog("syscall", "SYS_OPEN: no access mode in flags");
        return -(int32_t)EINVAL;
    }
    if (!g_fs) {
        klog("syscall", "SYS_OPEN: filesystem not mounted");
        return -(int32_t)EIO;
    }

    const char* name = path;
    if (name[0] == '/') name++;
    if (!*name) {
        klog("syscall", "SYS_OPEN: path is just '/'");
        return -(int32_t)EINVAL;
    }

    const uint16_t dir_cluster = 0;  /* always root for now */
    fat16_dirent_t entry;
    int rc = fat16_stat(g_fs, dir_cluster, name, &entry);

    if (rc == FAT16_ERR_NOT_FOUND) {
        if (!(flags & O_CREATE)) {
            klog("syscall", "SYS_OPEN: not found");
            return -(int32_t)ENOENT;
        }
        rc = fat16_touch(g_fs, dir_cluster, name);
        if (rc == FAT16_ERR_INVALID)  return -(int32_t)EINVAL;
        if (rc == FAT16_ERR_NOSPACE)  return -(int32_t)ENOSPC;
        if (rc != FAT16_OK)           return -(int32_t)EIO;

        rc = fat16_stat(g_fs, dir_cluster, name, &entry);
        if (rc != FAT16_OK)           return -(int32_t)EIO;
    } else if (rc == FAT16_ERR_INVALID) {
        return -(int32_t)EINVAL;
    } else if (rc != FAT16_OK) {
        return -(int32_t)EIO;
    }

    if (entry.attr & FAT16_ATTR_DIRECTORY)
        return -(int32_t)EISDIR;

    int fd = fd_alloc(syscall_get_fd_table(), dir_cluster, entry.name, flags, entry.size);
    klog_dec("syscall", "SYS_OPEN fd", (uint32_t)(fd >= 0 ? fd : 0));
    return (int32_t)fd;
}

/* -----------------------------------------------------------------------
 * SYS_CLOSE (6): ecx = fd
 * ----------------------------------------------------------------------- */
int32_t sys_close(int32_t fd) {
    klog_dec("syscall", "SYS_CLOSE fd", (uint32_t)fd);
    return (int32_t)fd_close(syscall_get_fd_table(), (int)fd);
}

/* Static kernel buffer for SYS_FREAD — prevents user pointer from being
 * passed directly to fat16_read_file so we can bounds-check first. */
#define FREAD_BUF_SIZE (8 * 1024)
static char fread_buf[FREAD_BUF_SIZE];

/* -----------------------------------------------------------------------
 * SYS_FREAD (7): ecx = fd, edx = buf, ebx = max_len
 *
 * Reads up to max_len bytes from fd at fd->offset.
 * Advances fd->offset by the number of bytes returned.
 * Returns 0 at EOF.
 * ----------------------------------------------------------------------- */
int32_t sys_fread(int32_t fd, char* buf, uint32_t max_len) {
    klog_dec("syscall", "SYS_FREAD fd", (uint32_t)fd);

    if (max_len == 0) return 0;
    if (!user_ptr_ok(buf, max_len)) {
        klog("syscall", "SYS_FREAD: invalid buf pointer");
        return -(int32_t)EINVAL;
    }
    if (!g_fs) {
        klog("syscall", "SYS_FREAD: filesystem not mounted");
        return -(int32_t)EIO;
    }

    file_descriptor_t* f = fd_get(syscall_get_fd_table(), (int)fd);
    if (!f) {
        klog("syscall", "SYS_FREAD: invalid fd");
        return -(int32_t)EBADF;
    }
    if (!(f->flags & O_READ)) {
        klog("syscall", "SYS_FREAD: fd not readable");
        return -(int32_t)EACCES;
    }

    /* Re-stat to pick up any size changes (e.g. written by SYS_FWRITE) */
    fat16_dirent_t dirent;
    if (fat16_stat(g_fs, f->dir_cluster, f->name, &dirent) == FAT16_OK)
        f->size = dirent.size;

    klog_dec("syscall", "SYS_FREAD offset", f->offset);
    klog_dec("syscall", "SYS_FREAD size",   f->size);

    if (f->offset >= f->size) return 0;  /* EOF */

    uint32_t out_len = 0;
    int rc = fat16_read_file(g_fs, f->dir_cluster, f->name,
                             fread_buf, FREAD_BUF_SIZE, &out_len);
    if (rc != FAT16_OK) {
        klog("syscall", "SYS_FREAD: fat16_read_file failed");
        return -(int32_t)EIO;
    }

    uint32_t avail = (out_len > f->offset) ? (out_len - f->offset) : 0U;
    uint32_t n     = (avail < max_len) ? avail : max_len;

    for (uint32_t i = 0; i < n; i++)
        buf[i] = fread_buf[f->offset + i];

    f->offset += n;
    klog_dec("syscall", "SYS_FREAD bytes", n);
    return (int32_t)n;
}

/* -----------------------------------------------------------------------
 * SYS_FWRITE (8): ecx = fd, edx = buf, ebx = len
 *
 * Writes len bytes from buf into the file at fd->offset using
 * fat16_write_at(), which supports partial overwrite, append, and
 * extension beyond EOF.  fd->offset and fd->size are advanced/updated
 * on success.
 * ----------------------------------------------------------------------- */
int32_t sys_fwrite(int32_t fd, const char* buf, uint32_t len) {
    klog_dec("syscall", "SYS_FWRITE fd",  (uint32_t)fd);
    klog_dec("syscall", "SYS_FWRITE len", len);

    if (len == 0) return 0;
    if (len > 0x8000) len = 0x8000;  /* cap at 32 KB per call */
    if (!user_ptr_ok(buf, len)) {
        klog("syscall", "SYS_FWRITE: invalid buf pointer");
        return -(int32_t)EINVAL;
    }
    if (!g_fs) {
        klog("syscall", "SYS_FWRITE: filesystem not mounted");
        return -(int32_t)EIO;
    }

    file_descriptor_t* f = fd_get(syscall_get_fd_table(), (int)fd);
    if (!f) {
        klog("syscall", "SYS_FWRITE: invalid fd");
        return -(int32_t)EBADF;
    }
    if (!(f->flags & O_WRITE)) {
        klog("syscall", "SYS_FWRITE: fd not writable");
        return -(int32_t)EACCES;
    }

    klog_dec("syscall", "SYS_FWRITE offset", f->offset);

    int rc = fat16_write_at(g_fs, f->dir_cluster, f->name, f->offset, buf, len);
    if (rc != FAT16_OK) {
        klog("syscall", "SYS_FWRITE: fat16_write_at failed");
        return -(int32_t)EIO;
    }

    f->offset += len;
    if (f->offset > f->size) f->size = f->offset;

    klog_dec("syscall", "SYS_FWRITE wrote", len);
    return (int32_t)len;
}
