#include "kmalloc.h"

#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

static uint8_t* heap_start = 0;
static uint8_t* heap_end = 0;
static uint8_t* heap_ptr = 0;

void kmalloc_init(uint32_t heap_start_addr) {
    heap_start = (uint8_t*)heap_start_addr;
    heap_end = heap_start + KMALLOC_HEAP_SIZE;
    heap_ptr = heap_start;
}

void* kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size = ALIGN(size);

    if (heap_ptr + size > heap_end) {
        return NULL;
    }

    void* ptr = heap_ptr;
    heap_ptr += size;

    return ptr;
}

void kfree(void* ptr) {
    (void)ptr;
}
