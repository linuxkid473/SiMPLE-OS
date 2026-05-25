#ifndef SIMPLE_SYSCALL_H
#define SIMPLE_SYSCALL_H

#include "fat16.h"
#include "fd.h"

/* Filesystem binding — called from shell_run() after fat16_mount() */
void        syscall_set_fs(fat16_fs_t *fs);
fd_table_t *syscall_get_fd_table(void);

/* New syscalls — kernel-side function signatures */
int32_t sys_getpid(void);
int32_t sys_getticks(void);
int32_t sys_stat(const char *path, void *out);
int32_t sys_readdir(const char *path, void *buf, uint32_t max_entries);
int32_t sys_rename(const char *old_path, const char *new_path);

/* SYS_SLEEP is dispatched via proc_sleep() in process.h, not here */

#endif
