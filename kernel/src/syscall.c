/*
 * syscall.c — kernel-side syscall implementations.
 *
 * Linux i386 ABI: ebx=arg0, ecx=arg1, edx=arg2, esi=arg3, edi=arg4
 */

#include "console.h"
#include "elf.h"
#include "fat16.h"
#include "fd.h"
#include "gdt.h"
#include "keyboard.h"   /* kb_scancode_available, proc_block_on_kbd */
#include "klog.h"
#include "paging.h"
#include "pipe.h"
#include "pit.h"
#include "posix_errno.h"
#include "process.h"
#include "registers.h"
#include "serial.h"
#include "signal.h"
#include "string.h"
#include "tty.h"
#include "types.h"
#include "vga.h"

/* Valid userspace pointer range.
 * USER_ADDR_MIN : bottom of user ELF region  (= USER_BASE, 0x300000)
 * USER_ADDR_MAX : top of user heap ceiling   (= PROC_BRK_MAX, 0x700000)
 * Anything outside this window is either kernel memory or unmapped. */
#define USER_ADDR_MIN  0x300000UL
#define USER_ADDR_MAX  0x700000UL   /* must equal PROC_BRK_MAX */

static fat16_fs_t *g_fs          = NULL;
static fd_table_t  g_fd_table;
static int         g_fd_table_ok = 0;

void syscall_set_fs(fat16_fs_t *fs) {
    g_fs = fs;
    if (!g_fd_table_ok) {
        fd_table_init(&g_fd_table);
        g_fd_table_ok = 1;
    }
}

fd_table_t *syscall_get_fd_table(void) {
    if (current_proc >= 0 && proc_table[current_proc].state != PROC_DEAD)
        return &proc_table[current_proc].fd_table;
    if (!g_fd_table_ok) {
        fd_table_init(&g_fd_table);
        g_fd_table_ok = 1;
    }
    return &g_fd_table;
}

static int user_ptr_ok(const void *ptr, uint32_t len) {
    uint32_t addr = (uint32_t)ptr;
    if (!ptr)                           return 0;
    if (addr < USER_ADDR_MIN)           return 0;
    if (addr > USER_ADDR_MAX)           return 0;   /* in kernel or unmapped range */
    if (len > 0) {
        uint32_t end = addr + len;
        if (end < addr)                 return 0;   /* wrap-around overflow */
        if (end > USER_ADDR_MAX)        return 0;   /* past user ceiling */
    }
    return 1;
}

/* ---- LEGACY syscalls (kept for elf.c / old code paths) ---- */

int32_t sys_write(const char *buf, uint32_t len) {
    if (len == 0) return 0;
    if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
    if (len > 0x8000) len = 0x8000;
    for (uint32_t i = 0; i < len; i++)
        vga_putc(buf[i]);
    return (int32_t)len;
}

int32_t sys_read(char *buf, uint32_t max_len) {
    if (max_len == 0) return 0;
    if (!user_ptr_ok(buf, max_len)) return -(int32_t)EINVAL;
    console_read_line(buf, max_len);
    return (int32_t)strlen(buf);
}

static const char *strip_slash(const char *path) {
    if (path && path[0] == '/') return path + 1;
    return path;
}

int32_t sys_open(const char *path, uint32_t flags) {
    if (!path || !user_ptr_ok(path, 1)) return -(int32_t)EINVAL;
    if (!*path) return -(int32_t)EINVAL;
    /* Map old flags to new: if neither read nor write set, default to read */
    if (!(flags & (O_READ | O_WRITE | O_RDONLY | O_WRONLY | O_RDWR)))
        flags |= O_RDONLY;
    if (!g_fs) return -(int32_t)EIO;

    const char *name = strip_slash(path);
    if (!*name) return -(int32_t)EINVAL;

    fat16_dirent_t entry;
    int rc = fat16_stat(g_fs, 0, name, &entry);

    if (rc == FAT16_ERR_NOT_FOUND) {
        if (!(flags & (O_CREATE | O_CREAT))) return -(int32_t)ENOENT;
        rc = fat16_touch(g_fs, 0, name);
        if (rc != FAT16_OK) return -(int32_t)EIO;
        rc = fat16_stat(g_fs, 0, name, &entry);
        if (rc != FAT16_OK) return -(int32_t)EIO;
    } else if (rc != FAT16_OK) {
        return -(int32_t)EIO;
    }

    if (entry.attr & FAT16_ATTR_DIRECTORY) return -(int32_t)EISDIR;

    int fd = fd_alloc(syscall_get_fd_table(), 0, entry.name, flags, entry.size);
    return (int32_t)fd;
}

int32_t sys_close(int32_t fd) {
    return (int32_t)fd_close(syscall_get_fd_table(), (int)fd);
}

/* File-read staging buffer.  Kernel BSS; must stay < 0x200000 (kmalloc base).
 * 64 KB handles all text/config files; ELF loading uses a separate path. */
#define FREAD_BUF_SIZE (64 * 1024)
static char fread_buf[FREAD_BUF_SIZE];

int32_t sys_fread(int32_t fd, char *buf, uint32_t max_len) {
    if (max_len == 0) return 0;
    if (!user_ptr_ok(buf, max_len)) return -(int32_t)EINVAL;
    if (!g_fs) return -(int32_t)EIO;

    file_desc_t *f = fd_get(syscall_get_fd_table(), (int)fd);
    if (!f || f->type != FD_FILE) return -(int32_t)EBADF;
    if ((f->flags & O_ACCMODE) == O_WRONLY) return -(int32_t)EACCES;

    fat16_dirent_t dirent;
    if (fat16_stat(g_fs, f->file.dir_cluster, f->file.name, &dirent) == FAT16_OK)
        f->file.size = dirent.size;

    if (f->file.offset >= f->file.size) return 0;

    uint32_t out_len = 0;
    int rc = fat16_read_file(g_fs, f->file.dir_cluster, f->file.name,
                             fread_buf, FREAD_BUF_SIZE, &out_len);
    if (rc != FAT16_OK) return -(int32_t)EIO;

    uint32_t avail = (out_len > f->file.offset) ? (out_len - f->file.offset) : 0U;
    uint32_t n     = (avail < max_len) ? avail : max_len;

    for (uint32_t i = 0; i < n; i++)
        buf[i] = fread_buf[f->file.offset + i];

    f->file.offset += n;
    return (int32_t)n;
}

int32_t sys_fwrite(int32_t fd, const char *buf, uint32_t len) {
    if (len == 0) return 0;
    if (len > 0x8000) len = 0x8000;
    if (!user_ptr_ok(buf, len)) return -(int32_t)EINVAL;
    if (!g_fs) return -(int32_t)EIO;

    file_desc_t *f = fd_get(syscall_get_fd_table(), (int)fd);
    if (!f || f->type != FD_FILE) return -(int32_t)EBADF;
    if ((f->flags & O_ACCMODE) == O_RDONLY) return -(int32_t)EACCES;

    int rc = fat16_write_at(g_fs, f->file.dir_cluster, f->file.name,
                            f->file.offset, buf, len);
    if (rc != FAT16_OK) return -(int32_t)EIO;

    f->file.offset += len;
    if (f->file.offset > f->file.size) f->file.size = f->file.offset;
    return (int32_t)len;
}

int32_t sys_seek(int32_t fd, int32_t offset, int32_t whence) {
    file_desc_t *f = fd_get(syscall_get_fd_table(), (int)fd);
    if (!f || f->type != FD_FILE) return -(int32_t)EBADF;

    int32_t new_pos;
    if (whence == SEEK_SET) {
        if (offset < 0) return -(int32_t)EINVAL;
        new_pos = offset;
    } else if (whence == SEEK_CUR) {
        new_pos = (int32_t)f->file.offset + offset;
        if (new_pos < 0) return -(int32_t)EINVAL;
    } else if (whence == SEEK_END) {
        if (g_fs) {
            fat16_dirent_t dirent;
            if (fat16_stat(g_fs, f->file.dir_cluster, f->file.name, &dirent) == FAT16_OK)
                f->file.size = dirent.size;
        }
        new_pos = (int32_t)f->file.size + offset;
        if (new_pos < 0) return -(int32_t)EINVAL;
    } else {
        return -(int32_t)EINVAL;
    }

    f->file.offset = (uint32_t)new_pos;
    return new_pos;
}

/* Sigreturn trampoline: mov $119, %eax; int $0x80; hlt */
static const uint8_t sigreturn_trampoline[] = {
    0xB8, 0x77, 0x00, 0x00, 0x00,  /* mov $119, %eax (SYS_SIGRETURN) */
    0xCD, 0x80,                      /* int $0x80 */
    0xF4                             /* hlt */
};

/* Exit stub: mov $1, %eax; xor %ebx,%ebx; int $0x80; hlt */
static const uint8_t exit_stub_bytes[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,  /* mov $1, %eax */
    0x31, 0xDB,                      /* xor %ebx, %ebx */
    0xCD, 0x80,                      /* int $0x80 */
    0xF4                             /* hlt */
};

static void plant_user_stubs(void) {
    uint8_t *tramp = (uint8_t *)SIG_TRAMPOLINE_ADDR;
    for (uint32_t i = 0; i < sizeof(sigreturn_trampoline); i++)
        tramp[i] = sigreturn_trampoline[i];

    uint8_t *xstub = (uint8_t *)EXIT_STUB_ADDR;
    for (uint32_t i = 0; i < sizeof(exit_stub_bytes); i++)
        xstub[i] = exit_stub_bytes[i];
}

/*
 * sys_exec — replace the current process image with a new ELF.
 *
 * Buffer strategy: we read the ELF file directly into user space (0x300000),
 * which is 1 MB and can hold any program on disk.  Because exec() is about
 * to overwrite user space anyway, reusing it as a load staging area is safe.
 *
 * The ELF segment copy is forward (dest ≤ src for our linker layout,
 * p_vaddr=0x300000 / p_offset=0x60), so in-place copy never reads from
 * an address it has already overwritten.  All segment metadata is saved into
 * local variables before the first byte is copied, so clobbering the ELF
 * header during the copy is harmless.
 *
 * This path is used by child processes after fork()+exec(); the kernel's
 * shell uses exec_elf() directly.
 */
int32_t sys_exec(const char *path, registers_t *regs) {
    if (!user_ptr_ok(path, 1)) return -(int32_t)EFAULT;

    const char *name = strip_slash(path);
    if (!*name) return -(int32_t)EINVAL;
    if (!g_fs)  return -(int32_t)EIO;

    /*
     * Read ELF file directly into user space.  The current page directory
     * (whether kernel's or a child's after fork) maps virtual 0x300000 as
     * writable from ring-0, so fat16_read_file can write here safely.
     */
    uint8_t  *load_buf = (uint8_t *)USER_BASE;          /* 0x300000 */
    uint32_t  max_read  = USER_STACK - USER_BASE;        /* 1 MB     */
    uint32_t  out_len   = 0;
    int rc = fat16_read_file(g_fs, 0, name, (char *)load_buf, max_read, &out_len);
    if (rc != FAT16_OK) return -(int32_t)ENOENT;
    if (out_len < sizeof(Elf32_Ehdr)) return -(int32_t)ENOEXEC;

    if (elf_validate(load_buf) != 0) return -(int32_t)ENOEXEC;

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)load_buf;
    Elf32_Phdr *phdr = (Elf32_Phdr *)(load_buf + ehdr->e_phoff);

    /* Sanity-check phdr pointer stays within what we read */
    uint32_t phdr_end = (uint32_t)ehdr->e_phoff +
                        (uint32_t)ehdr->e_phnum * sizeof(Elf32_Phdr);
    if (phdr_end > out_len) return -(int32_t)ENOEXEC;

    /* Compute load bias from first PT_LOAD */
    uint32_t base = 0;
    int found_load = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            base = USER_BASE - phdr[i].p_vaddr;
            found_load = 1;
            break;
        }
    }
    if (!found_load) return -(int32_t)ENOEXEC;

    /* Validate all PT_LOAD segments stay within user space */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t dst_start = phdr[i].p_vaddr + base;
        uint32_t dst_end   = dst_start + phdr[i].p_memsz;
        if (dst_start < USER_BASE || dst_end > USER_STACK)
            return -(int32_t)ENOEXEC;
        if (phdr[i].p_filesz > phdr[i].p_memsz)
            return -(int32_t)ENOEXEC;  /* malformed */
        if (phdr[i].p_offset + phdr[i].p_filesz < phdr[i].p_offset)
            return -(int32_t)ENOEXEC;  /* overflow */
    }

    /* Save entry + segment descriptors BEFORE in-place copy clobbers ELF headers */
    uint32_t entry = ehdr->e_entry + base;
    if (entry < USER_BASE || entry >= USER_STACK) return -(int32_t)ENOEXEC;

    typedef struct { uint32_t dest, filesz, memsz, src_off; } seg_t;
    seg_t saved[8];
    int nseg = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum && nseg < 8; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint32_t avail = (phdr[i].p_offset <= out_len)
                         ? (out_len - phdr[i].p_offset) : 0U;
        saved[nseg].dest    = phdr[i].p_vaddr + base;
        saved[nseg].filesz  = (phdr[i].p_filesz <= avail)
                               ? phdr[i].p_filesz : avail;
        saved[nseg].memsz   = phdr[i].p_memsz;
        saved[nseg].src_off = phdr[i].p_offset;
        nseg++;
    }

    /*
     * In-place segment copy.  For our ELF layout (p_vaddr=0x300000,
     * p_offset=0x60), dest == load_buf and src == load_buf+0x60, so the
     * forward copy (dest[j] = src[j], j ascending) never reads past what
     * has already been written.
     */
    for (int i = 0; i < nseg; i++) {
        uint8_t *dest = (uint8_t *)saved[i].dest;
        uint8_t *src  = load_buf + saved[i].src_off;
        for (uint32_t j = 0; j < saved[i].filesz; j++) dest[j] = src[j];
        for (uint32_t j = saved[i].filesz; j < saved[i].memsz; j++) dest[j] = 0;
    }

    /* Reset brk to start of heap for the newly exec'd program */
    if (current_proc >= 0)
        proc_table[current_proc].brk = 0x400000U;

    /* Close any O_CLOEXEC file descriptors */
    if (current_proc >= 0) {
        fd_table_t *fdt = &proc_table[current_proc].fd_table;
        for (int fd = 0; fd < FD_MAX; fd++) {
            if (fdt->fds[fd].type != FD_NONE && fdt->fds[fd].cloexec)
                fd_close(fdt, fd);
        }
    }

    /* Plant kernel stubs in user stub area */
    plant_user_stubs();

    /* Build POSIX initial stack */
    uint32_t new_sp = build_posix_stack(name);

    /* Redirect iret to the new entry point */
    regs->eip     = entry;
    regs->cs      = SEG_UCODE;
    regs->eflags  = 0x202;        /* IF=1, always */
    regs->useresp = new_sp;
    regs->ss      = SEG_UDATA;

    return 0;
}

#define PROC_BRK_MAX 0x700000U

int32_t sys_sbrk(int32_t increment) {
    if (current_proc < 0) return -1;

    process_t *proc = &proc_table[current_proc];
    uint32_t old_brk = proc->brk;

    if (increment <= 0) return (int32_t)old_brk;

    uint32_t new_brk = old_brk + (uint32_t)increment;
    if (new_brk > PROC_BRK_MAX || new_brk < old_brk) return -1;

    for (uint32_t addr = old_brk & ~0xFFFU; addr < new_brk; addr += 0x1000U) {
        if (paging_page_mapped(proc->page_dir, addr)) continue;
        uint32_t phys = paging_alloc_phys_page();
        if (!phys) return -1;
        paging_map_page(proc->page_dir, addr, phys, 1);
    }

    proc->brk = new_brk;
    return (int32_t)old_brk;
}

int32_t sys_getpid(void) {
    if (current_proc < 0) return -1;
    return (int32_t)proc_table[current_proc].pid;
}

int32_t sys_getticks(void) {
    return (int32_t)pit_ticks();
}

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

int32_t sys_stat(const char *path, sys_stat_t *out) {
    if (!user_ptr_ok(path, 1)) return -(int32_t)EINVAL;
    if (!user_ptr_ok(out, (uint32_t)sizeof(sys_stat_t))) return -(int32_t)EINVAL;
    if (!g_fs) return -(int32_t)EIO;

    const char *name = strip_slash(path);

    fat16_dirent_t entry;
    int rc = fat16_stat(g_fs, 0, name, &entry);

    if (rc == FAT16_ERR_NOT_FOUND) {
        out->size   = 0;
        out->is_dir = 0;
        out->exists = 0;
        return 0;
    }
    if (rc != FAT16_OK) return -(int32_t)EIO;

    out->size   = entry.size;
    out->is_dir = (entry.attr & FAT16_ATTR_DIRECTORY) ? 1 : 0;
    out->exists = 1;
    return 0;
}

#define READDIR_KERNEL_MAX 64
static fat16_dirent_t readdir_kbuf[READDIR_KERNEL_MAX];

int32_t sys_readdir(const char *path, sys_dirent_t *out, uint32_t max_entries) {
    if (!user_ptr_ok(path, 1)) return -(int32_t)EINVAL;
    if (max_entries == 0) return 0;
    if (max_entries > READDIR_KERNEL_MAX) max_entries = READDIR_KERNEL_MAX;
    if (!user_ptr_ok(out, max_entries * (uint32_t)sizeof(sys_dirent_t)))
        return -(int32_t)EINVAL;
    if (!g_fs) return -(int32_t)EIO;

    uint16_t dir_cluster = 0;
    const char *dname = strip_slash(path);

    if (*dname != '\0') {
        fat16_dirent_t de;
        if (fat16_stat(g_fs, 0, dname, &de) != FAT16_OK) return -1;
        if (!(de.attr & FAT16_ATTR_DIRECTORY)) return -(int32_t)EINVAL;
        dir_cluster = de.first_cluster;
    }

    int count = 0;
    int rc = fat16_list_entries(g_fs, dir_cluster, readdir_kbuf,
                                (int)max_entries, &count);
    if (rc != FAT16_OK) return -1;

    for (int i = 0; i < count; i++) {
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

int32_t sys_rename(const char *old_path, const char *new_path) {
    if (!user_ptr_ok(old_path, 1) || !user_ptr_ok(new_path, 1)) return -(int32_t)EINVAL;
    if (!g_fs) return -(int32_t)EIO;

    const char *old_name = strip_slash(old_path);
    const char *new_name = strip_slash(new_path);
    if (!*old_name || !*new_name) return -(int32_t)EINVAL;

    fat16_dirent_t old_entry;
    if (fat16_stat(g_fs, 0, old_name, &old_entry) != FAT16_OK) return -(int32_t)ENOENT;

    int rc = fat16_move_file(g_fs, 0, old_name, 0, new_name);
    return (rc == FAT16_OK) ? 0 : -1;
}

/* ---- NEW POSIX syscall implementations ---- */

int32_t sys_linux_write(int32_t fd, const char *buf, uint32_t len) {
    if (len == 0) return 0;
    if (len > 0x8000) len = 0x8000;

    if (fd == 1 || fd == 2) {
        /* Check if stdout/stderr has been redirected via dup2 to a pipe */
        file_desc_t *f01 = fd_get(syscall_get_fd_table(), fd);
        if (f01 && f01->type == FD_PIPE_W) {
            if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
            return (int32_t)pipe_write(f01->pipe.pipe_idx, buf, len);
        }
        /* stdout / stderr: write to VGA */
        if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
        for (uint32_t i = 0; i < len; i++)
            vga_putc(buf[i]);
        return (int32_t)len;
    }

    /* Look up fd in process table */
    file_desc_t *f = fd_get(syscall_get_fd_table(), fd);
    if (!f) return -(int32_t)EBADF;

    if (f->type == FD_TTY) {
        if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
        for (uint32_t i = 0; i < len; i++)
            vga_putc(buf[i]);
        return (int32_t)len;
    }

    if (f->type == FD_PIPE_W) {
        if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
        return (int32_t)pipe_write(f->pipe.pipe_idx, buf, len);
    }

    if (f->type == FD_FILE) {
        if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
        if ((f->flags & O_ACCMODE) == O_RDONLY) return -(int32_t)EACCES;
        if (!g_fs) return -(int32_t)EIO;

        /* Handle O_APPEND */
        if (f->flags & O_APPEND) {
            fat16_dirent_t dirent;
            if (fat16_stat(g_fs, f->file.dir_cluster, f->file.name, &dirent) == FAT16_OK)
                f->file.offset = dirent.size;
        }

        int rc = fat16_write_at(g_fs, f->file.dir_cluster, f->file.name,
                                f->file.offset, buf, len);
        if (rc != FAT16_OK) return -(int32_t)EIO;
        f->file.offset += len;
        if (f->file.offset > f->file.size) f->file.size = f->file.offset;
        return (int32_t)len;
    }

    return -(int32_t)EBADF;
}

int32_t sys_linux_read(int32_t fd, char *buf, uint32_t len, registers_t *regs) {
    if (len == 0) return 0;
    if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;

    if (fd == 0) {
        /* stdin: keyboard.
         *
         * If the scancode ring buffer is empty AND other processes are
         * runnable, block this process (PROC_BLOCKED) and switch away.
         * keyboard_irq_handler() will set it PROC_RUNNABLE when a key
         * arrives, and the re-executed int $0x80 will retry from scratch.
         *
         * This prevents process 0's stdin poll from starving GUI processes
         * by holding the CPU in a ring-0 busy loop. */
        if (!kb_scancode_available() && current_proc >= 0) {
            proc_block_on_kbd(regs);
            /* If we returned (no other process to switch to), fall through
             * and call console_read_line normally. */
        }

        if (tty_is_canon()) {
            /* Canonical mode: read a full line */
            console_read_line(buf, len);
            /* Check if line contains Ctrl-C */
            uint32_t line_len = (uint32_t)strlen(buf);
            for (uint32_t i = 0; i < line_len; i++) {
                if (tty_is_intr(buf[i])) {
                    if (current_proc >= 0)
                        proc_send_signal(proc_table[current_proc].pid, SIGINT);
                    return -(int32_t)EINTR;
                }
                if (tty_is_eof(buf[i])) {
                    return 0;  /* EOF */
                }
            }
            /* Append newline if not already there */
            if (line_len < len - 1) {
                buf[line_len] = '\n';
                buf[line_len + 1] = '\0';
                return (int32_t)(line_len + 1);
            }
            return (int32_t)line_len;
        } else {
            /* Raw mode */
            uint8_t vmin = g_tty.termios.c_cc[VMIN];
            if (vmin == 0) vmin = 1;
            uint32_t got = 0;
            while (got < vmin && got < len) {
                char c = keyboard_getchar();
                if (tty_is_intr(c)) {
                    if (current_proc >= 0)
                        proc_send_signal(proc_table[current_proc].pid, SIGINT);
                    if (got == 0) return -(int32_t)EINTR;
                    break;
                }
                buf[got++] = c;
            }
            return (int32_t)got;
        }
    }

    /* Other fds */
    file_desc_t *f = fd_get(syscall_get_fd_table(), fd);
    if (!f) return -(int32_t)EBADF;

    if (f->type == FD_TTY) {
        /* Same as fd 0 */
        console_read_line(buf, len);
        return (int32_t)strlen(buf);
    }

    if (f->type == FD_PIPE_R) {
        int idx = f->pipe.pipe_idx;
        int r   = pipe_read(idx, buf, len);
        if (r == 0 && g_pipes[idx].writer_open > 0) {
            /* No data yet but write end is open — block until writer writes */
            if (current_proc >= 0) {
                if (proc_table[current_proc].sig_pending &
                    ~proc_table[current_proc].sig_mask)
                    return -(int32_t)EINTR;
                proc_block_on_pipe(idx, regs);
                /* After wake-up we retry via eip-2 re-entry; fall through
                 * returning 0 handles the single-process edge case. */
            }
        }
        return (int32_t)r;
    }

    if (f->type == FD_FILE) {
        if ((f->flags & O_ACCMODE) == O_WRONLY) return -(int32_t)EACCES;
        if (!g_fs) return -(int32_t)EIO;

        fat16_dirent_t dirent;
        if (fat16_stat(g_fs, f->file.dir_cluster, f->file.name, &dirent) == FAT16_OK)
            f->file.size = dirent.size;

        if (f->file.offset >= f->file.size) return 0;

        uint32_t out_len = 0;
        int rc = fat16_read_file(g_fs, f->file.dir_cluster, f->file.name,
                                 fread_buf, FREAD_BUF_SIZE, &out_len);
        if (rc != FAT16_OK) return -(int32_t)EIO;

        uint32_t avail = (out_len > f->file.offset) ? (out_len - f->file.offset) : 0U;
        uint32_t n     = (avail < len) ? avail : len;

        for (uint32_t i = 0; i < n; i++)
            buf[i] = fread_buf[f->file.offset + i];

        f->file.offset += n;
        return (int32_t)n;
    }

    return -(int32_t)EBADF;
}

int32_t sys_linux_open(const char *path, uint32_t flags, uint32_t mode) {
    (void)mode;
    if (!user_ptr_ok(path, 1)) return -(int32_t)EFAULT;
    if (!*path) return -(int32_t)EINVAL;
    if (!g_fs) return -(int32_t)EIO;

    const char *name = strip_slash(path);
    if (!*name) return -(int32_t)EINVAL;

    fat16_dirent_t entry;
    int rc = fat16_stat(g_fs, 0, name, &entry);

    if (rc == FAT16_ERR_NOT_FOUND) {
        if (!(flags & O_CREAT)) return -(int32_t)ENOENT;
        rc = fat16_touch(g_fs, 0, name);
        if (rc != FAT16_OK) return -(int32_t)EIO;
        rc = fat16_stat(g_fs, 0, name, &entry);
        if (rc != FAT16_OK) return -(int32_t)EIO;
    } else if (rc != FAT16_OK) {
        return -(int32_t)EIO;
    }

    if (entry.attr & FAT16_ATTR_DIRECTORY) {
        /* Allow opening directories for reading (getdents) */
        int fd = fd_alloc_file(syscall_get_fd_table(), entry.first_cluster,
                               entry.name, flags, 0);
        return (int32_t)fd;
    }

    if ((flags & O_TRUNC) && ((flags & O_ACCMODE) != O_RDONLY)) {
        fat16_write_file(g_fs, 0, name, "", 0);
        entry.size = 0;
    }

    int fd = fd_alloc_file(syscall_get_fd_table(), 0, entry.name, flags, entry.size);
    return (int32_t)fd;
}

int32_t sys_linux_close(int32_t fd) {
    return (int32_t)fd_close(syscall_get_fd_table(), fd);
}

int32_t sys_linux_lseek(int32_t fd, int32_t offset, int32_t whence) {
    file_desc_t *f = fd_get(syscall_get_fd_table(), fd);
    if (!f) return -(int32_t)EBADF;
    if (f->type == FD_PIPE_R || f->type == FD_PIPE_W) return -(int32_t)ESPIPE;
    if (f->type == FD_TTY) return -(int32_t)ESPIPE;
    return sys_seek(fd, offset, whence);
}

int32_t sys_linux_unlink(const char *path) {
    if (!user_ptr_ok(path, 1)) return -(int32_t)EFAULT;
    if (!g_fs) return -(int32_t)EIO;

    const char *name = strip_slash(path);
    if (!*name) return -(int32_t)EINVAL;

    fat16_dirent_t entry;
    if (fat16_stat(g_fs, 0, name, &entry) != FAT16_OK) return -(int32_t)ENOENT;
    if (entry.attr & FAT16_ATTR_DIRECTORY) return -(int32_t)EISDIR;

    int rc = fat16_remove(g_fs, 0, name);
    return (rc == FAT16_OK) ? 0 : -(int32_t)EIO;
}

int32_t sys_linux_rename(const char *old, const char *newp) {
    return sys_rename(old, newp);
}

int32_t sys_linux_mkdir(const char *path, uint32_t mode) {
    (void)mode;
    if (!user_ptr_ok(path, 1)) return -(int32_t)EFAULT;
    if (!g_fs) return -(int32_t)EIO;

    const char *name = strip_slash(path);
    if (!*name) return -(int32_t)EINVAL;

    int rc = fat16_mkdir(g_fs, 0, name);
    if (rc == FAT16_ERR_EXISTS) return -(int32_t)EEXIST;
    if (rc == FAT16_ERR_NOSPACE) return -(int32_t)ENOSPC;
    return (rc == FAT16_OK) ? 0 : -(int32_t)EIO;
}

int32_t sys_linux_rmdir(const char *path) {
    if (!user_ptr_ok(path, 1)) return -(int32_t)EFAULT;
    if (!g_fs) return -(int32_t)EIO;

    const char *name = strip_slash(path);
    if (!*name) return -(int32_t)EINVAL;

    fat16_dirent_t entry;
    if (fat16_stat(g_fs, 0, name, &entry) != FAT16_OK) return -(int32_t)ENOENT;
    if (!(entry.attr & FAT16_ATTR_DIRECTORY)) return -(int32_t)ENOTDIR;

    int rc = fat16_remove(g_fs, 0, name);
    if (rc == FAT16_ERR_NOTEMPTY) return -(int32_t)ENOTEMPTY;
    return (rc == FAT16_OK) ? 0 : -(int32_t)EIO;
}

int32_t sys_linux_dup(int32_t fd) {
    return (int32_t)fd_dup(syscall_get_fd_table(), fd);
}

int32_t sys_linux_dup2(int32_t oldfd, int32_t newfd) {
    return (int32_t)fd_dup2(syscall_get_fd_table(), oldfd, newfd);
}

int32_t sys_linux_pipe(int32_t *fds) {
    if (!user_ptr_ok(fds, 2 * sizeof(int32_t))) return -(int32_t)EFAULT;

    int pipe_idx = pipe_alloc();
    if (pipe_idx < 0) return -(int32_t)ENFILE;

    fd_table_t *fdt = syscall_get_fd_table();

    /* Read end */
    int rfd = fd_alloc_pipe(fdt, pipe_idx, 0);
    if (rfd < 0) {
        pipe_release(pipe_idx, 0);
        pipe_release(pipe_idx, 1);
        return -(int32_t)EMFILE;
    }

    /* Write end */
    int wfd = fd_alloc_pipe(fdt, pipe_idx, 1);
    if (wfd < 0) {
        fd_close(fdt, rfd);
        pipe_release(pipe_idx, 0);
        pipe_release(pipe_idx, 1);
        return -(int32_t)EMFILE;
    }

    /* We gave both ends refcount=1 each from pipe_alloc.
     * fd_alloc_pipe doesn't increment the refcount — that's fine since
     * pipe_alloc starts at reader_open=1, writer_open=1. */
    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

int32_t sys_linux_brk(uint32_t addr) {
    if (current_proc < 0) return -ENOMEM;

    process_t *proc = &proc_table[current_proc];

    if (addr == 0) return (int32_t)proc->brk;

    if (addr < 0x400000U) {
        /* Don't allow shrinking below heap start */
        return (int32_t)proc->brk;
    }

    if (addr > PROC_BRK_MAX) {
        return (int32_t)proc->brk;
    }

    if (addr > proc->brk) {
        /* Grow: map new pages */
        for (uint32_t a = proc->brk & ~0xFFFU; a < addr; a += 0x1000U) {
            if (paging_page_mapped(proc->page_dir, a)) continue;
            uint32_t phys = paging_alloc_phys_page();
            if (!phys) return (int32_t)proc->brk;
            paging_map_page(proc->page_dir, a, phys, 1);
        }
    }

    proc->brk = addr;
    return (int32_t)proc->brk;
}

int32_t sys_linux_ioctl(int32_t fd, uint32_t req, uint32_t arg) {
    /* Handle TTY ioctls */
    if (fd <= 2 || (fd_get(syscall_get_fd_table(), fd) &&
                    fd_get(syscall_get_fd_table(), fd)->type == FD_TTY)) {
        switch (req) {
        case TCGETS: {
            if (!user_ptr_ok((void *)arg, sizeof(termios_t))) return -(int32_t)EFAULT;
            termios_t *t = (termios_t *)arg;
            *t = g_tty.termios;
            return 0;
        }
        case TCSETS:
        case TCSETSW:
        case TCSETSF: {
            if (!user_ptr_ok((void *)arg, sizeof(termios_t))) return -(int32_t)EFAULT;
            g_tty.termios = *(termios_t *)arg;
            return 0;
        }
        case TIOCGWINSZ: {
            if (!user_ptr_ok((void *)arg, sizeof(struct winsize))) return -(int32_t)EFAULT;
            struct winsize *ws = (struct winsize *)arg;
            ws->ws_row    = (uint16_t)g_tty.rows;
            ws->ws_col    = (uint16_t)g_tty.cols;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP: {
            if (!user_ptr_ok((void *)arg, sizeof(pid_t))) return -(int32_t)EFAULT;
            *(pid_t *)arg = (current_proc >= 0) ? proc_table[current_proc].pgid : 1;
            return 0;
        }
        case TIOCSPGRP:
            return 0;
        case TIOCSCTTY:
            return 0;
        default:
            return -(int32_t)ENOTTY;
        }
    }
    return -(int32_t)ENOTTY;
}

int32_t sys_linux_fcntl(int32_t fd, int32_t cmd, uint32_t arg) {
    fd_table_t *fdt = syscall_get_fd_table();
    file_desc_t *f = fd_get(fdt, fd);
    if (!f) return -(int32_t)EBADF;

    switch (cmd) {
    case F_DUPFD:
        return (int32_t)fd_dup(fdt, fd);
    case F_GETFD:
        return f->cloexec ? FD_CLOEXEC : 0;
    case F_SETFD:
        f->cloexec = (arg & FD_CLOEXEC) ? 1 : 0;
        return 0;
    case F_GETFL:
        return (int32_t)f->flags;
    case F_SETFL:
        f->flags = (f->flags & O_ACCMODE) | (arg & ~O_ACCMODE);
        return 0;
    default:
        return -(int32_t)EINVAL;
    }
}

int32_t sys_linux_setpgid(pid_t pid, pgid_t pgid) {
    if (pid == 0) pid = (current_proc >= 0) ? proc_table[current_proc].pid : 1;
    if (pgid == 0) pgid = pid;

    int idx = proc_find_by_pid(pid);
    if (idx < 0) return -(int32_t)ESRCH;

    proc_table[idx].pgid = pgid;
    return 0;
}

int32_t sys_linux_setsid(void) {
    if (current_proc < 0) return -(int32_t)EPERM;
    pid_t pid = proc_table[current_proc].pid;
    proc_table[current_proc].sid  = pid;
    proc_table[current_proc].pgid = pid;
    return (int32_t)pid;
}

int32_t sys_linux_getppid(void) {
    if (current_proc < 0) return 1;
    return (int32_t)proc_table[current_proc].parent_pid;
}

int32_t sys_linux_getpgrp(void) {
    if (current_proc < 0) return 1;
    return (int32_t)proc_table[current_proc].pgid;
}

int32_t sys_linux_kill(pid_t pid, int sig) {
    if (pid > 0) {
        return (int32_t)proc_send_signal(pid, sig);
    } else if (pid == 0) {
        /* Send to process group */
        if (current_proc >= 0)
            proc_send_signal_group(proc_table[current_proc].pgid, sig);
        return 0;
    } else if (pid == -1) {
        /* Send to all processes */
        for (int i = 0; i < MAX_PROCS; i++) {
            if (proc_table[i].state != PROC_DEAD &&
                proc_table[i].pid != (current_proc >= 0 ? proc_table[current_proc].pid : -1))
                proc_send_signal(proc_table[i].pid, sig);
        }
        return 0;
    } else {
        proc_send_signal_group(-pid, sig);
        return 0;
    }
}

int32_t sys_linux_sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    if (sig <= 0 || sig >= NSIG) return -(int32_t)EINVAL;
    if (sig == SIGKILL || sig == SIGSTOP) return -(int32_t)EINVAL;
    if (current_proc < 0) return -(int32_t)ESRCH;

    if (oact) {
        if (!user_ptr_ok(oact, sizeof(struct sigaction))) return -(int32_t)EFAULT;
        *oact = proc_table[current_proc].sig_actions[sig];
    }

    if (act) {
        if (!user_ptr_ok(act, sizeof(struct sigaction))) return -(int32_t)EFAULT;
        proc_table[current_proc].sig_actions[sig] = *act;
    }

    return 0;
}

int32_t sys_linux_sigprocmask(int how, const uint32_t *set, uint32_t *oset) {
    if (current_proc < 0) return -(int32_t)ESRCH;

    uint32_t old_mask = proc_table[current_proc].sig_mask;

    if (oset) {
        if (!user_ptr_ok(oset, sizeof(uint32_t))) return -(int32_t)EFAULT;
        *oset = old_mask;
    }

    if (set) {
        if (!user_ptr_ok(set, sizeof(uint32_t))) return -(int32_t)EFAULT;
        uint32_t new_set = *set;
        /* SIGKILL and SIGSTOP cannot be blocked */
        new_set &= ~((1U << SIGKILL) | (1U << SIGSTOP));

        switch (how) {
        case SIG_BLOCK:
            proc_table[current_proc].sig_mask |= new_set;
            break;
        case SIG_UNBLOCK:
            proc_table[current_proc].sig_mask &= ~new_set;
            break;
        case SIG_SETMASK:
            proc_table[current_proc].sig_mask = new_set;
            break;
        default:
            return -(int32_t)EINVAL;
        }
    }

    return 0;
}

int32_t sys_linux_sigreturn(registers_t *regs) {
    if (current_proc < 0) return -(int32_t)ESRCH;

    /* Restore from sig_frame_t on user stack */
    uint32_t user_sp = regs->useresp;
    sig_frame_t *frame = (sig_frame_t *)user_sp;

    if (!user_ptr_ok(frame, sizeof(sig_frame_t))) return -(int32_t)EFAULT;

    regs->eax     = frame->saved_eax;
    regs->ecx     = frame->saved_ecx;
    regs->edx     = frame->saved_edx;
    regs->ebx     = frame->saved_ebx;
    regs->esi     = frame->saved_esi;
    regs->edi     = frame->saved_edi;
    regs->ebp     = frame->saved_ebp;
    regs->eip     = frame->saved_eip;
    regs->eflags  = frame->saved_eflags | 0x200;  /* keep IF set */
    regs->useresp = frame->saved_useresp;

    proc_table[current_proc].sig_mask = frame->saved_mask;

    return 0;
}

int32_t sys_linux_sigsuspend(const uint32_t *mask) {
    if (current_proc < 0) return -(int32_t)ESRCH;
    if (mask && user_ptr_ok(mask, sizeof(uint32_t))) {
        /* Temporarily set mask and wait for a signal */
        proc_table[current_proc].sig_mask = *mask &
            ~((1U << SIGKILL) | (1U << SIGSTOP));
    }
    /* Always returns -EINTR after signal delivery */
    return -(int32_t)EINTR;
}

int32_t sys_linux_sigpending(uint32_t *set) {
    if (!user_ptr_ok(set, sizeof(uint32_t))) return -(int32_t)EFAULT;
    if (current_proc < 0) {
        *set = 0;
        return 0;
    }
    *set = proc_table[current_proc].sig_pending;
    return 0;
}

int32_t sys_linux_rt_sigaction(int sig, const struct sigaction *act,
                                struct sigaction *oact, uint32_t sz) {
    (void)sz;
    return sys_linux_sigaction(sig, act, oact);
}

int32_t sys_linux_rt_sigprocmask(int how, const uint32_t *set,
                                  uint32_t *oset, uint32_t sz) {
    (void)sz;
    return sys_linux_sigprocmask(how, set, oset);
}

int32_t sys_linux_mmap2(uint32_t addr, uint32_t len, uint32_t prot,
                         uint32_t flags, int32_t fd, uint32_t pgoff) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)pgoff;
    if (len == 0) return -(int32_t)EINVAL;
    if (current_proc < 0) return -(int32_t)ENOMEM;

    /* Anonymous mmap: use brk to get memory */
    process_t *proc = &proc_table[current_proc];
    uint32_t old_brk = proc->brk;
    uint32_t new_brk = (old_brk + len + 0xFFFU) & ~0xFFFU;

    if (new_brk > PROC_BRK_MAX) return -(int32_t)ENOMEM;

    for (uint32_t a = old_brk & ~0xFFFU; a < new_brk; a += 0x1000U) {
        if (paging_page_mapped(proc->page_dir, a)) continue;
        uint32_t phys = paging_alloc_phys_page();
        if (!phys) return -(int32_t)ENOMEM;
        paging_map_page(proc->page_dir, a, phys, 1);
    }

    proc->brk = new_brk;
    return (int32_t)old_brk;
}

int32_t sys_linux_munmap(uint32_t addr, uint32_t len) {
    (void)addr; (void)len;
    return 0;  /* no-op */
}

int32_t sys_linux_chdir(const char *path) {
    if (!user_ptr_ok(path, 1)) return -(int32_t)EFAULT;
    if (current_proc < 0) return -(int32_t)ESRCH;

    const char *name = strip_slash(path);

    if (*name != '\0' && g_fs) {
        fat16_dirent_t entry;
        if (fat16_stat(g_fs, 0, name, &entry) != FAT16_OK) return -(int32_t)ENOENT;
        if (!(entry.attr & FAT16_ATTR_DIRECTORY)) return -(int32_t)ENOTDIR;
    }

    /* Update cwd */
    char *cwd = proc_table[current_proc].cwd;
    cwd[0] = '/';
    int i = 1;
    if (*name != '\0') {
        const char *p = name;
        while (*p && i < CWD_MAX - 1) cwd[i++] = *p++;
    }
    cwd[i] = '\0';
    return 0;
}

int32_t sys_linux_getcwd(char *buf, uint32_t len) {
    if (!user_ptr_ok(buf, len)) return -(int32_t)EFAULT;
    if (len == 0) return -(int32_t)EINVAL;

    const char *cwd = (current_proc >= 0) ? proc_table[current_proc].cwd : "/";
    uint32_t i = 0;
    while (cwd[i] && i < len - 1) { buf[i] = cwd[i]; i++; }
    buf[i] = '\0';
    return (int32_t)i;
}

/* struct utsname */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} utsname_t;

int32_t sys_linux_uname(void *buf) {
    if (!user_ptr_ok(buf, sizeof(utsname_t))) return -(int32_t)EFAULT;
    utsname_t *u = (utsname_t *)buf;

    /* Copy strings manually */
    const char *sysname = "SiMPLE";
    const char *nodename = "simpleos";
    const char *release  = "1.0";
    const char *version  = "1.0.0";
    const char *machine  = "i686";
    const char *domain   = "";

    int i = 0;
    for (i = 0; sysname[i] && i < 64; i++) u->sysname[i] = sysname[i];
    u->sysname[i] = '\0';
    for (i = 0; nodename[i] && i < 64; i++) u->nodename[i] = nodename[i];
    u->nodename[i] = '\0';
    for (i = 0; release[i] && i < 64; i++) u->release[i] = release[i];
    u->release[i] = '\0';
    for (i = 0; version[i] && i < 64; i++) u->version[i] = version[i];
    u->version[i] = '\0';
    for (i = 0; machine[i] && i < 64; i++) u->machine[i] = machine[i];
    u->machine[i] = '\0';
    for (i = 0; domain[i] && i < 64; i++) u->domainname[i] = domain[i];
    u->domainname[i] = '\0';

    return 0;
}

/* struct timespec */
typedef struct {
    int32_t tv_sec;
    int32_t tv_nsec;
} timespec_t;

int32_t sys_linux_nanosleep(const void *req, void *rem, registers_t *regs) {
    if (!user_ptr_ok(req, sizeof(timespec_t))) return -(int32_t)EFAULT;
    const timespec_t *rq = (const timespec_t *)req;

    /* Convert to PIT ticks (100 Hz) */
    uint32_t ticks = (uint32_t)rq->tv_sec * 100U;
    ticks += (uint32_t)(rq->tv_nsec / 10000000L); /* nanosecs to hundredths */

    if (rem) {
        if (user_ptr_ok(rem, sizeof(timespec_t))) {
            timespec_t *rm = (timespec_t *)rem;
            rm->tv_sec  = 0;
            rm->tv_nsec = 0;
        }
    }

    if (ticks == 0) return 0;

    /*
     * Use proc_sleep() so that other processes can run while this one sleeps.
     * proc_sleep saves the current register frame, marks this process as
     * PROC_SLEEPING, and switches to the next runnable process.  When the
     * PIT timer wakes this process again it resumes here with eax=0.
     * Falls back to a single-process HLT loop when no other process exists.
     */
    proc_sleep(regs, ticks);
    return 0;
}

/* struct timeval */
typedef struct {
    int32_t tv_sec;
    int32_t tv_usec;
} timeval_t;

int32_t sys_linux_gettimeofday(void *tv, void *tz) {
    (void)tz;
    if (tv) {
        if (!user_ptr_ok(tv, sizeof(timeval_t))) return -(int32_t)EFAULT;
        timeval_t *t = (timeval_t *)tv;
        uint32_t ticks = pit_ticks();
        t->tv_sec  = (int32_t)(ticks / 100U);
        t->tv_usec = (int32_t)((ticks % 100U) * 10000);
    }
    return 0;
}

/* struct timespec for clock_gettime */
int32_t sys_linux_clock_gettime(int clk, void *tp) {
    (void)clk;
    if (!user_ptr_ok(tp, sizeof(timespec_t))) return -(int32_t)EFAULT;
    timespec_t *t = (timespec_t *)tp;
    uint32_t ticks = pit_ticks();
    t->tv_sec  = (int32_t)(ticks / 100U);
    t->tv_nsec = (int32_t)((ticks % 100U) * 10000000L);
    return 0;
}

/* Linux dirent for getdents */
typedef struct {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char     d_name[1]; /* variable */
} __attribute__((packed)) linux_dirent_t;

int32_t sys_linux_getdents(int32_t fd, void *buf, uint32_t count) {
    if (!user_ptr_ok(buf, count)) return -(int32_t)EFAULT;
    if (!g_fs) return -(int32_t)EIO;

    file_desc_t *f = fd_get(syscall_get_fd_table(), fd);
    if (!f) return -(int32_t)EBADF;
    if (f->type != FD_FILE) return -(int32_t)ENOTDIR;

    /* List entries */
    fat16_dirent_t kbuf[32];
    int kcount = 0;
    int rc = fat16_list_entries(g_fs, f->file.dir_cluster, kbuf, 32, &kcount);
    if (rc != FAT16_OK) return -(int32_t)EIO;

    uint32_t pos = 0;
    uint32_t ino = 1;
    for (int i = (int)f->file.offset; i < kcount && pos + 12 + 14 < count; i++) {
        uint32_t name_len = (uint32_t)strlen(kbuf[i].name);
        uint32_t reclen = (8 + name_len + 2 + 3) & ~3U;
        if (pos + reclen > count) break;

        linux_dirent_t *de = (linux_dirent_t *)((uint8_t *)buf + pos);
        de->d_ino    = ino++;
        de->d_off    = pos + reclen;
        de->d_reclen = (uint16_t)reclen;
        uint32_t j = 0;
        while (j < name_len) { de->d_name[j] = kbuf[i].name[j]; j++; }
        de->d_name[j] = '\0';
        /* file type byte at end of name */
        de->d_name[j + 1] = (kbuf[i].attr & FAT16_ATTR_DIRECTORY) ? 4 : 8;

        pos += reclen;
        f->file.offset++;
    }

    return (int32_t)pos;
}

/* Linux dirent64 */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[1];
} __attribute__((packed)) linux_dirent64_t;

int32_t sys_linux_getdents64(int32_t fd, void *buf, uint32_t count) {
    if (!user_ptr_ok(buf, count)) return -(int32_t)EFAULT;
    if (!g_fs) return -(int32_t)EIO;

    file_desc_t *f = fd_get(syscall_get_fd_table(), fd);
    if (!f) return -(int32_t)EBADF;

    uint16_t dir_cluster = 0;
    if (f->type == FD_FILE) {
        dir_cluster = f->file.dir_cluster;
    }

    fat16_dirent_t kbuf[32];
    int kcount = 0;
    int rc = fat16_list_entries(g_fs, dir_cluster, kbuf, 32, &kcount);
    if (rc != FAT16_OK) return -(int32_t)EIO;

    uint32_t pos = 0;
    uint64_t ino = 1;
    for (int i = (int)f->file.offset; i < kcount; i++) {
        uint32_t name_len = (uint32_t)strlen(kbuf[i].name);
        uint32_t reclen = (19 + name_len + 3) & ~3U;
        if (pos + reclen > count) break;

        linux_dirent64_t *de = (linux_dirent64_t *)((uint8_t *)buf + pos);
        de->d_ino    = ino++;
        de->d_off    = (int64_t)(pos + reclen);
        de->d_reclen = (uint16_t)reclen;
        de->d_type   = (kbuf[i].attr & FAT16_ATTR_DIRECTORY) ? 4 : 8;
        uint32_t j = 0;
        while (j < name_len) { de->d_name[j] = kbuf[i].name[j]; j++; }
        de->d_name[j] = '\0';

        pos += reclen;
        f->file.offset++;
    }

    return (int32_t)pos;
}

int32_t sys_linux_llseek(int32_t fd, uint32_t off_hi, uint32_t off_lo,
                          uint64_t *result, uint32_t whence) {
    (void)off_hi;
    int32_t r = sys_linux_lseek(fd, (int32_t)off_lo, (int32_t)whence);
    if (r < 0) return r;
    if (result && user_ptr_ok(result, sizeof(uint64_t)))
        *result = (uint64_t)(uint32_t)r;
    return 0;
}

/* struct pollfd */
typedef struct {
    int32_t  fd;
    int16_t  events;
    int16_t  revents;
} pollfd_t;

int32_t sys_linux_poll(void *fds, uint32_t nfds, int32_t timeout) {
    (void)timeout;
    if (!user_ptr_ok(fds, nfds * sizeof(pollfd_t))) return -(int32_t)EFAULT;

    pollfd_t *pfds = (pollfd_t *)fds;
    int ready = 0;

    for (uint32_t i = 0; i < nfds; i++) {
        pfds[i].revents = 0;
        if (pfds[i].fd < 0) continue;

        file_desc_t *f = fd_get(syscall_get_fd_table(), pfds[i].fd);
        if (!f) {
            pfds[i].revents = POLLNVAL;
            ready++;
            continue;
        }

        /* For pipes: check if data available */
        if (f->type == FD_PIPE_R) {
            if ((pfds[i].events & POLLIN) && pipe_read_avail(f->pipe.pipe_idx) > 0) {
                pfds[i].revents |= POLLIN;
                ready++;
            }
            if (g_pipes[f->pipe.pipe_idx].writer_open == 0) {
                pfds[i].revents |= POLLHUP;
                ready++;
            }
        } else if (f->type == FD_PIPE_W) {
            if (pfds[i].events & POLLOUT) {
                pfds[i].revents |= POLLOUT;
                ready++;
            }
        } else if (f->type == FD_TTY || pfds[i].fd <= 2) {
            if (pfds[i].events & POLLIN) {
                pfds[i].revents |= POLLIN;
                ready++;
            }
            if (pfds[i].events & POLLOUT) {
                pfds[i].revents |= POLLOUT;
                ready++;
            }
        } else if (f->type == FD_FILE) {
            if (pfds[i].events & POLLIN) {
                pfds[i].revents |= POLLIN;
                ready++;
            }
            if (pfds[i].events & POLLOUT) {
                pfds[i].revents |= POLLOUT;
                ready++;
            }
        }
    }

    return ready;
}

int32_t sys_linux_wait4(pid_t pid, int *status, int options, void *rusage,
                         registers_t *regs) {
    (void)rusage;
    return (int32_t)proc_waitpid(pid, status, options, regs);
}

int32_t sys_linux_execve(const char *path, char **argv, char **envp,
                          registers_t *regs) {
    (void)argv; (void)envp;
    /* For now, just do the basic exec without argv/envp */
    return sys_exec(path, regs);
}

int32_t sys_linux_getuid32(void) {
    if (current_proc >= 0) return (int32_t)proc_table[current_proc].uid;
    return 0;
}

int32_t sys_linux_getgid32(void) {
    if (current_proc >= 0) return (int32_t)proc_table[current_proc].gid;
    return 0;
}

int32_t sys_linux_geteuid32(void) {
    if (current_proc >= 0) return (int32_t)proc_table[current_proc].euid;
    return 0;
}

int32_t sys_linux_getegid32(void) {
    if (current_proc >= 0) return (int32_t)proc_table[current_proc].egid;
    return 0;
}

int32_t sys_linux_prctl(uint32_t opt, uint32_t a1, uint32_t a2,
                         uint32_t a3, uint32_t a4) {
    (void)opt; (void)a1; (void)a2; (void)a3; (void)a4;
    return 0;
}

int32_t sys_linux_umask(uint32_t mask) {
    if (current_proc < 0) return 022;
    uint32_t old = proc_table[current_proc].umask;
    proc_table[current_proc].umask = mask & 0777U;
    return (int32_t)old;
}

int32_t sys_powerctl(int mode) {
    (void)mode;
    /* power off / reboot stub */
    __asm__ volatile("cli; hlt");
    return 0;
}
