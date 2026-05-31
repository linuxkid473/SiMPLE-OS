#ifndef SIMPLE_KMALLOC_H
#define SIMPLE_KMALLOC_H

#include "types.h"

#define KMALLOC_HEAP_SIZE 0x1A0000  /* 1.7 MB: kernel heap up to 0x400000 boundary */

void kmalloc_init(uint32_t heap_start);
void* kmalloc(size_t size);
void kfree(void* ptr);
void kmalloc_reset(void);
uint32_t kmalloc_used(void);
uint32_t kmalloc_total(void);

#endif
