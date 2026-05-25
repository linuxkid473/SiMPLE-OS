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
 *   2  SYS_EXIT   — terminate program (handled in idt.c via exit_trampoline)
 *   3  SYS_READ   — blocking line input
 *   4  SYS_YIELD  — cooperative yield (no-op)
 *   5  SYS_OPEN   — open file → fd
 *   6  SYS_CLOSE  — release fd
 *   7  SYS_FREAD  — read from fd
 *   8  SYS_FWRITE — write to fd
 *   9  SYS_SEEK   — reposition fd offset
 *  10  SYS_EXEC   — replace current process image with a new ELF
 *                   ecx = path (const char*, user pointer)
 *                   does not return on success; returns -errno on failure
 */

#include "console.h"
#include "elf.h"
#include "fat16.h"
#include "fd.h"
#include "gdt.h"
#include "klog.h"
#include "paging.h"
#include "pit.h"
#include "process.h"
#include "registers.h"
#include "serial.h"
#include "string.h"
#include "vga.h"
#include "types.h"

/*
 * Minimum valid userspace pointer.  Must match USER_BASE in elf.c.
 * User programs load at 0x300000; their stack is below 0x400000.
 * Pointers below 0x300000 are into kernel or heap space — reject them.
 */
#define USER_ADDR_MIN 0x300000UL

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
    /* If a process is running, use its per-process fd table. */
    if (current_proc >= 0 && proc_table[current_proc].state != PROC_DEAD)
        return &proc_table[current_proc].fd_table;
    /* Fallback for kernel-only context (e.g. shell-level operations). */
    if (!g_fd_table_ok) {
        fd_table_init(&g_fd_table);
        g_fd_table_ok = 1;
    }
    return &g_fd_table;
}

/* Upper bound of the user address space: heap can grow to PROC_BRK_MAX.
 * User code+data starts at USER_ADDR_MIN; stack sits just below 0x400000;
 * heap runs from 0x400000 up to PROC_BRK_MAX.  Anything above is kernel. */
#define USER_ADDR_MAX 0x700000UL   /* must equal PROC_BRK_MAX in syscall.c */

/* Returns 1 if [ptr, ptr+len) is a plausible userspace buffer. */
static int user_ptr_ok(const void* ptr, uint32_t len) {
    uint32_t addr = (uint32_t)ptr;
    if (!ptr)                              return 0;
    if (addr < USER_ADDR_MIN)              return 0;
    if (len > 0 && (addr + len) < addr)   return 0;  /* integer wrap */
    if (len > 0 && (addr + len) > USER_ADDR_MAX) return 0;  /* above heap */
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
 * SYS_YIELD (4): handled directly in idt.c's syscall_handler via proc_yield().
 * This stub is kept so nothing breaks if called directly.
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

/* -----------------------------------------------------------------------
 * SYS_SEEK (9): ecx = fd, edx = offset (signed), ebx = whence
 *
 * Repositions fd->offset according to whence:
 *   SEEK_SET(0): new offset = offset
 *   SEEK_CUR(1): new offset = fd->offset + offset
 *   SEEK_END(2): new offset = file_size + offset
 *
 * Seeking beyond EOF is allowed (no cluster allocation).
 * Negative final offsets are rejected with -EINVAL.
 * fd->size is NOT modified by seek.
 * ----------------------------------------------------------------------- */
int32_t sys_seek(int32_t fd, int32_t offset, int32_t whence) {
    klog_dec("syscall", "SYS_SEEK fd",     (uint32_t)fd);
    klog_dec("syscall", "SYS_SEEK offset", (uint32_t)offset);
    klog_dec("syscall", "SYS_SEEK whence", (uint32_t)whence);

    file_descriptor_t* f = fd_get(syscall_get_fd_table(), (int)fd);
    if (!f) {
        klog("syscall", "SYS_SEEK: invalid fd");
        return -(int32_t)EBADF;
    }

    int32_t new_pos;

    if (whence == SEEK_SET) {
        if (offset < 0) {
            klog("syscall", "SYS_SEEK: negative SEEK_SET offset");
            return -(int32_t)EINVAL;
        }
        new_pos = offset;
    } else if (whence == SEEK_CUR) {
        new_pos = (int32_t)f->offset + offset;
        if (new_pos < 0) {
            klog("syscall", "SYS_SEEK: negative result from SEEK_CUR");
            return -(int32_t)EINVAL;
        }
    } else if (whence == SEEK_END) {
        /* Re-stat to pick up the latest on-disk size */
        if (g_fs) {
            fat16_dirent_t dirent;
            if (fat16_stat(g_fs, f->dir_cluster, f->name, &dirent) == FAT16_OK)
                f->size = dirent.size;
        }
        new_pos = (int32_t)f->size + offset;
        if (new_pos < 0) {
            klog("syscall", "SYS_SEEK: negative result from SEEK_END");
            return -(int32_t)EINVAL;
        }
    } else {
        klog("syscall", "SYS_SEEK: invalid whence");
        return -(int32_t)EINVAL;
    }

    uint32_t old_offset = f->offset;
    f->offset = (uint32_t)new_pos;
    klog_dec("syscall", "SYS_SEEK old", old_offset);
    klog_dec("syscall", "SYS_SEEK new", f->offset);
    return new_pos;
}

/* -----------------------------------------------------------------------
 * SYS_EXEC (10): ecx = path (const char*, user pointer)
 *
 * Replaces the current user-space image with a new ELF executable loaded
 * from the FAT16 filesystem.  On success this syscall does NOT return to
 * the calling program — the ISR's iret frame is patched to jump directly
 * to the new ELF entry point at ring3.  On failure -errno is returned
 * normally and the original program continues.
 *
 * The kernel stack context (kernel_esp, saved_ebp/ebx/esi/edi) saved by
 * launch_ring3() is preserved across exec, so when the new program
 * eventually calls SYS_EXIT, exit_trampoline restores exec_elf's frame
 * exactly as if the original program had exited.
 * ----------------------------------------------------------------------- */

/* Kernel-side read buffer — lives in supervisor memory, never overlaps user space. */
#define EXEC_BUF_SIZE (64 * 1024)
static uint8_t exec_buf[EXEC_BUF_SIZE];

/* Machine code for the ring3 exit stub planted just below the new stack top.
 * If _start returns without calling SYS_EXIT this stub runs instead. */
static const uint8_t exec_exit_stub[] = {
    0xB8, 0x02, 0x00, 0x00, 0x00,  /* mov $2, %eax   (SYS_EXIT) */
    0x31, 0xC9,                      /* xor %ecx, %ecx            */
    0xCD, 0x80,                      /* int $0x80                 */
    0xF4                             /* hlt            (safety)   */
};
#define EXEC_STUB_ADDR (USER_STACK - 32U)

int32_t sys_exec(const char *path, registers_t *regs) {
    klog("syscall", "SYS_EXEC");

    /* --- 1. Validate the user-supplied path pointer ------------------- */
    if (!user_ptr_ok(path, 1)) {
        klog("syscall", "SYS_EXEC: invalid path pointer");
        return -(int32_t)EINVAL;
    }

    const char *name = path;
    if (name[0] == '/') name++;
    if (!*name) {
        klog("syscall", "SYS_EXEC: empty path");
        return -(int32_t)EINVAL;
    }

    if (!g_fs) {
        klog("syscall", "SYS_EXEC: filesystem not mounted");
        return -(int32_t)EIO;
    }

    klog("syscall", "SYS_EXEC: reading file");
    serial_write(COM1, "[SIMPLE] SYS_EXEC: path=");
    serial_write(COM1, name);
    serial_write(COM1, "\n");

    /* --- 2. Read the ELF file into the kernel exec buffer ------------- */
    uint32_t out_len = 0;
    int rc = fat16_read_file(g_fs, 0, name, exec_buf, EXEC_BUF_SIZE, &out_len);
    if (rc != FAT16_OK) {
        klog("syscall", "SYS_EXEC: file not found or read error");
        return -(int32_t)ENOENT;
    }
    klog_dec("syscall", "SYS_EXEC: file bytes", out_len);

    /* --- 3. Validate ELF header --------------------------------------- */
    if (elf_validate(exec_buf) != 0) {
        klog("syscall", "SYS_EXEC: invalid ELF");
        return -(int32_t)ENOEXEC;
    }

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)exec_buf;
    Elf32_Phdr *phdr = (Elf32_Phdr *)(exec_buf + ehdr->e_phoff);

    /* Compute load bias: shift first PT_LOAD segment to USER_BASE. */
    uint32_t base = 0;
    int found_load = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            base = USER_BASE - phdr[i].p_vaddr;
            found_load = 1;
            break;
        }
    }
    if (!found_load) {
        klog("syscall", "SYS_EXEC: no PT_LOAD segment");
        return -(int32_t)ENOEXEC;
    }

    /* --- 4. Bounds-check all load segments before touching user space - */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t dest_start = phdr[i].p_vaddr + base;
        uint32_t dest_end   = dest_start + phdr[i].p_memsz;
        if (dest_start < USER_BASE || dest_end > USER_STACK) {
            klog_hex("syscall", "SYS_EXEC: segment out of bounds", dest_start);
            return -(int32_t)ENOEXEC;
        }
    }

    klog("syscall", "SYS_EXEC: ELF valid, loading new image");

    /* --- 5. Copy new ELF segments into user space --------------------- */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint8_t *dest = (uint8_t *)(phdr[i].p_vaddr + base);
        uint8_t *src  = exec_buf + phdr[i].p_offset;
        for (uint32_t j = 0; j < phdr[i].p_filesz; j++) dest[j] = src[j];
        for (uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) dest[j] = 0;
    }

    uint32_t entry = ehdr->e_entry + base;
    klog_hex("syscall", "SYS_EXEC: new entry", entry);

    /* --- 6. Plant exit stub and set up new user stack ----------------- */
    uint8_t *stub = (uint8_t *)EXEC_STUB_ADDR;
    for (uint32_t i = 0; i < sizeof(exec_exit_stub); i++) stub[i] = exec_exit_stub[i];

    uint32_t new_sp = USER_STACK - 4U - 32U; /* one slot below the stub */
    *(uint32_t *)new_sp = EXEC_STUB_ADDR;     /* _start return address   */

    klog_hex("syscall", "SYS_EXEC: new_sp", new_sp);

    /* --- 7. Patch the ISR iret frame to jump to the new entry point --- *
     *                                                                     *
     * The frame on the kernel ISR stack (kstack, at TSS.esp0) currently  *
     * holds the old program's [EIP, CS, EFLAGS, ESP, SS].  We overwrite  *
     * it so the isr_syscall epilogue's iret jumps straight into the new   *
     * ELF instead of returning to the old program.  The old code/data at  *
     * USER_BASE has already been replaced above, so there is nothing left  *
     * for the old program to execute even if somehow control returned.    */
    regs->eip     = entry;
    regs->cs      = SEG_UCODE;   /* 0x1B — user code, DPL=3  */
    regs->eflags  = 0x02;        /* reserved bit; IF=0        */
    regs->useresp = new_sp;
    regs->ss      = SEG_UDATA;   /* 0x23 — user data, DPL=3  */

    serial_write(COM1, "[SIMPLE] SYS_EXEC: iret frame patched → new program\n");

    /* Return value ends up in regs->eax via popa, but the new _start
     * never inspects eax, so the value is irrelevant. */
    return 0;
}

/* -----------------------------------------------------------------------
 * SYS_SBRK (15): ecx = increment (bytes, must be > 0)
 *
 * Grows the calling process's heap by `increment` bytes.
 * Returns the old break value (start of newly usable memory).
 * Returns -1 if the heap would exceed 0x700000 or physical pages run out.
 *
 * Each process starts with brk = 0x400000.  Pages are lazily mapped into
 * the process's PDE[1] page table as user-accessible (ring-3 R/W).
 * ----------------------------------------------------------------------- */
#define PROC_BRK_MAX 0x700000U

int32_t sys_sbrk(int32_t increment) {
    if (current_proc < 0) {
        klog("syscall", "SYS_SBRK: no current process");
        return -1;
    }

    process_t *proc = &proc_table[current_proc];
    uint32_t old_brk = proc->brk;

    if (increment <= 0) return (int32_t)old_brk;

    uint32_t new_brk = old_brk + (uint32_t)increment;

    if (new_brk > PROC_BRK_MAX || new_brk < old_brk) {
        klog("syscall", "SYS_SBRK: heap limit exceeded");
        return -1;
    }

    /* Map any 4 KB pages newly required to cover [old_brk, new_brk). */
    for (uint32_t addr = old_brk & ~0xFFFU; addr < new_brk; addr += 0x1000U) {
        if (paging_page_mapped(proc->page_dir, addr)) continue;
        uint32_t phys = paging_alloc_phys_page();
        if (!phys) {
            klog("syscall", "SYS_SBRK: out of physical pages");
            return -1;
        }
        paging_map_page(proc->page_dir, addr, phys, 1 /*user*/);
    }

    proc->brk = new_brk;

    serial_write(COM1, "[sbrk] old=");
    serial_write_hex(COM1, old_brk);
    serial_write(COM1, " new=");
    serial_write_hex(COM1, new_brk);
    serial_write(COM1, "\n");

    return (int32_t)old_brk;
}

/* -----------------------------------------------------------------------
 * SYS_GETPID (13): no arguments
 *
 * Returns the PID of the calling process.  Returns -1 if no process is
 * active (should not happen from ring3, but handle gracefully).
 * ----------------------------------------------------------------------- */
int32_t sys_getpid(void) {
    if (current_proc < 0 || current_proc >= MAX_PROCS) return -1;
    return (int32_t)proc_table[current_proc].pid;
}

/* -----------------------------------------------------------------------
 * SYS_GETTICKS (19): no arguments
 *
 * Returns the global PIT tick counter (100 Hz, same counter used by
 * SYS_SLEEP).  Useful for busy-wait timers and benchmarking from user space.
 * ----------------------------------------------------------------------- */
int32_t sys_getticks(void) {
    return (int32_t)pit_ticks();
}

/* -----------------------------------------------------------------------
 * Stat and readdir struct layouts — must match user/syscall.h exactly.
 *
 * sys_stat_t  (SYS_STAT result):
 *   uint32_t size    — file size in bytes (0 for dirs)
 *   uint8_t  is_dir  — 1 if directory, 0 if regular file
 *   uint8_t  exists  — 1 if the path was found, 0 if not
 *
 * sys_dirent_t  (SYS_READDIR per-entry):
 *   char     name[64] — null-terminated filename (8.3 format)
 *   uint8_t  is_dir   — 1 if directory
 *   uint8_t  _pad[3]  — alignment padding (compiler inserts this anyway)
 *   uint32_t size     — file size in bytes
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32_t size;
    uint8_t  is_dir;
    uint8_t  exists;
} sys_stat_t;

typedef struct {
    char     name[64];
    uint8_t  is_dir;
    uint8_t  _pad[3];
    uint32_t size;
} sys_dirent_t;

/* -----------------------------------------------------------------------
 * SYS_STAT (16): ecx = path (user ptr), edx = pointer to sys_stat_t
 *
 * Fills the caller's stat struct from the FAT16 directory entry.
 * Always looks up from the root directory (cluster 0).
 * Returns 0 on success, -1 if not found.
 * ----------------------------------------------------------------------- */
int32_t sys_stat(const char *path, sys_stat_t *out) {
    if (!user_ptr_ok(path, 1)) {
        klog("syscall", "SYS_STAT: invalid path pointer");
        return -(int32_t)EINVAL;
    }
    if (!user_ptr_ok(out, (uint32_t)sizeof(sys_stat_t))) {
        klog("syscall", "SYS_STAT: invalid out pointer");
        return -(int32_t)EINVAL;
    }
    if (!g_fs) {
        klog("syscall", "SYS_STAT: filesystem not mounted");
        return -(int32_t)EIO;
    }

    const char *name = path;
    if (name[0] == '/') name++;

    fat16_dirent_t entry;
    int rc = fat16_stat(g_fs, 0, name, &entry);

    if (rc == FAT16_ERR_NOT_FOUND) {
        out->size   = 0;
        out->is_dir = 0;
        out->exists = 0;
        return 0;
    }
    if (rc != FAT16_OK) {
        klog("syscall", "SYS_STAT: fat16_stat failed");
        return -(int32_t)EIO;
    }

    out->size   = entry.size;
    out->is_dir = (entry.attr & FAT16_ATTR_DIRECTORY) ? 1 : 0;
    out->exists = 1;
    return 0;
}

/* Kernel-side scratch buffer for SYS_READDIR — avoids holding a large
 * array on the stack and prevents user pointer from reaching fat16 directly. */
#define READDIR_KERNEL_MAX 64
static fat16_dirent_t readdir_kbuf[READDIR_KERNEL_MAX];

/* -----------------------------------------------------------------------
 * SYS_READDIR (17): ecx = path (user ptr), edx = sys_dirent_t[] (user ptr),
 *                   ebx = max entries
 *
 * Enumerates the directory at `path` (root-relative; "/" means root).
 * Writes up to max_entries entries into the caller's buffer.
 * Returns the number of entries written, or -1 on error.
 * ----------------------------------------------------------------------- */
int32_t sys_readdir(const char *path, sys_dirent_t *out, uint32_t max_entries) {
    if (!user_ptr_ok(path, 1)) {
        klog("syscall", "SYS_READDIR: invalid path pointer");
        return -(int32_t)EINVAL;
    }
    if (max_entries == 0) return 0;
    if (max_entries > READDIR_KERNEL_MAX) max_entries = READDIR_KERNEL_MAX;
    if (!user_ptr_ok(out, max_entries * (uint32_t)sizeof(sys_dirent_t))) {
        klog("syscall", "SYS_READDIR: invalid out pointer");
        return -(int32_t)EINVAL;
    }
    if (!g_fs) {
        klog("syscall", "SYS_READDIR: filesystem not mounted");
        return -(int32_t)EIO;
    }

    /* Resolve directory cluster from path */
    uint16_t dir_cluster = 0;
    const char *dname = path;
    if (dname[0] == '/') dname++;

    if (*dname != '\0') {
        fat16_dirent_t de;
        int rc = fat16_stat(g_fs, 0, dname, &de);
        if (rc != FAT16_OK) {
            klog("syscall", "SYS_READDIR: directory not found");
            return -1;
        }
        if (!(de.attr & FAT16_ATTR_DIRECTORY)) {
            klog("syscall", "SYS_READDIR: path is not a directory");
            return -(int32_t)EINVAL;
        }
        dir_cluster = de.first_cluster;
    }

    int count = 0;
    int rc = fat16_list_entries(g_fs, dir_cluster, readdir_kbuf,
                                (int)max_entries, &count);
    if (rc != FAT16_OK) {
        klog("syscall", "SYS_READDIR: fat16_list_entries failed");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        /* Copy 8.3 name — fat16_dirent_t.name is already null-terminated */
        uint32_t j = 0;
        while (readdir_kbuf[i].name[j] && j < 63) {
            out[i].name[j] = readdir_kbuf[i].name[j];
            j++;
        }
        out[i].name[j] = '\0';
        out[i].is_dir  = (readdir_kbuf[i].attr & FAT16_ATTR_DIRECTORY) ? 1 : 0;
        out[i]._pad[0] = 0;
        out[i]._pad[1] = 0;
        out[i]._pad[2] = 0;
        out[i].size    = readdir_kbuf[i].size;
    }

    return (int32_t)count;
}

/* -----------------------------------------------------------------------
 * SYS_RENAME (18): ecx = old_path (user ptr), edx = new_path (user ptr)
 *
 * Renames a file or directory within the root directory.
 * Uses fat16_move_file with the same source and destination directory
 * (cluster 0) to perform an in-place rename.
 * Returns 0 on success, -1 on error.
 * ----------------------------------------------------------------------- */
int32_t sys_rename(const char *old_path, const char *new_path) {
    if (!user_ptr_ok(old_path, 1)) {
        klog("syscall", "SYS_RENAME: invalid old_path pointer");
        return -(int32_t)EINVAL;
    }
    if (!user_ptr_ok(new_path, 1)) {
        klog("syscall", "SYS_RENAME: invalid new_path pointer");
        return -(int32_t)EINVAL;
    }
    if (!g_fs) {
        klog("syscall", "SYS_RENAME: filesystem not mounted");
        return -(int32_t)EIO;
    }

    const char *old_name = old_path;
    if (old_name[0] == '/') old_name++;
    const char *new_name = new_path;
    if (new_name[0] == '/') new_name++;

    if (!*old_name || !*new_name) {
        klog("syscall", "SYS_RENAME: empty path component");
        return -(int32_t)EINVAL;
    }

    fat16_dirent_t old_entry;
    if (fat16_stat(g_fs, 0, old_name, &old_entry) != FAT16_OK) {
        klog("syscall", "SYS_RENAME: old path not found");
        return -(int32_t)ENOENT;
    }

    int rc = fat16_move_file(g_fs, 0, old_name, 0, new_name);
    return (rc == FAT16_OK) ? 0 : -1;
}
