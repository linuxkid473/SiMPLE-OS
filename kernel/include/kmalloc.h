#ifndef SIMPLE_KMALLOC_H
#define SIMPLE_KMALLOC_H

#include "types.h"

#define KMALLOC_HEAP_SIZE 0x100000

void kmalloc_init(uint32_t heap_start);
void* kmalloc(size_t size);
void kfree(void* ptr);

#endif
