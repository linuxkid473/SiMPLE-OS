#ifndef SIMPLE_EDITOR_H
#define SIMPLE_EDITOR_H

#include "types.h"
#include "fat16.h"

void editor_open(fat16_fs_t* fs, uint16_t cwd_cluster, const char* filename);

#endif
