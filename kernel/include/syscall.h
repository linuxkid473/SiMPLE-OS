#ifndef SIMPLE_SYSCALL_H
#define SIMPLE_SYSCALL_H

#include "fat16.h"
#include "fd.h"

void       syscall_set_fs(fat16_fs_t* fs);
fd_table_t* syscall_get_fd_table(void);

#endif
