// user/malloc.c — simple bump allocator backed by SYS_SBRK

#include "malloc.h"

int sbrk(int increment);

typedef struct {
    size_t size;
} alloc_hdr_t;

void *malloc(size_t size) {
    if (size == 0) return (void *)0;

    /* Round total up to 8-byte alignment so subsequent allocations
     * are naturally aligned. */
    size_t total = sizeof(alloc_hdr_t) + size;
    total = (total + 7U) & ~7U;

    int base = sbrk((int)total);
    if (base == -1) return (void *)0;

    alloc_hdr_t *hdr = (alloc_hdr_t *)(unsigned int)base;
    hdr->size = size;
    return (void *)((char *)hdr + sizeof(alloc_hdr_t));
}

void free(void *ptr) {
    /* No-op: bump allocator does not reclaim memory. */
    (void)ptr;
}
