#ifndef SIMPLE_SHELL_H
#define SIMPLE_SHELL_H

#include "fat16.h"
#include "types.h"

void shell_run(fat16_fs_t* fs, int fs_ready);
void shell_set_boot_info(uint32_t memory_kb, int memory_known, int multiboot_ok);

#endif
