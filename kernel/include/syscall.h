#ifndef SIMPLE_SYSCALL_H
#define SIMPLE_SYSCALL_H

#include "fat16.h"
#include "fd.h"
#include "registers.h"

/* Filesystem binding — called from shell_run() after fat16_mount() */
void        syscall_set_fs(fat16_fs_t *fs);
fd_table_t *syscall_get_fd_table(void);

/* Syscall implementations — kernel-side */
int32_t sys_getpid(void);
int32_t sys_getticks(void);
int32_t sys_stat(const char *path, void *out);
int32_t sys_readdir(const char *path, void *buf, uint32_t max_entries);
int32_t sys_rename(const char *old_path, const char *new_path);
int32_t sys_fread(int32_t fd, char *buf, uint32_t max_len);
int32_t sys_fwrite(int32_t fd, const char *buf, uint32_t len);
int32_t sys_seek(int32_t fd, int32_t offset, int32_t whence);
int32_t sys_open(const char *path, uint32_t flags);
int32_t sys_close(int32_t fd);
int32_t sys_exec(const char *path, registers_t *regs);
int32_t sys_sbrk(int32_t increment);
int32_t sys_write(const char *buf, uint32_t len);
int32_t sys_read(char *buf, uint32_t max_len);

#endif
