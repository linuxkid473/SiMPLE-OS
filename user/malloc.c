// user/malloc.c — free-list allocator backed by sbrk().
//
// Replaces the original bump allocator whose free() was a no-op and whose
// realloc() copied beyond the old allocation.  A long-running program that
// allocates and frees constantly (e.g. an editor) would otherwise exhaust
// the 3 MB process heap in minutes.
//
// Design: first-fit over an address-ordered free list with block splitting
// and coalescing of adjacent free blocks.  Every block carries a header
// with its TOTAL size (header included), so free()/realloc() know exactly
// how big the allocation is.

#include "malloc.h"

void *sbrk(int increment);

#define ALIGN8(x)   (((x) + 7u) & ~7u)

typedef struct mblock {
    size_t         size;   /* total block size in bytes, header included */
    struct mblock *next;   /* next free block (valid only while free)    */
} mblock_t;

#define HDR_SIZE    ALIGN8(sizeof(mblock_t))
#define MIN_SPLIT   (HDR_SIZE + 16u)   /* smallest worthwhile remainder */
#define GROW_CHUNK  16384u             /* sbrk granularity */

static mblock_t *free_list = 0;        /* address-ordered */

/* Insert block into the address-ordered free list and coalesce with
 * neighbours that are physically adjacent. */
static void insert_free(mblock_t *b) {
    mblock_t *prev = 0;
    mblock_t *cur  = free_list;
    while (cur && cur < b) {
        prev = cur;
        cur  = cur->next;
    }

    /* Coalesce forward: b directly precedes cur */
    if (cur && (char *)b + b->size == (char *)cur) {
        b->size += cur->size;
        b->next  = cur->next;
    } else {
        b->next = cur;
    }

    /* Coalesce backward: prev directly precedes b */
    if (prev && (char *)prev + prev->size == (char *)b) {
        prev->size += b->size;
        prev->next  = b->next;
    } else if (prev) {
        prev->next = b;
    } else {
        free_list = b;
    }
}

void *malloc(size_t size) {
    if (size == 0) return (void *)0;

    size_t need = HDR_SIZE + ALIGN8(size);

    /* First fit */
    mblock_t **pp = &free_list;
    for (mblock_t *b = free_list; b; pp = &b->next, b = b->next) {
        if (b->size >= need) {
            if (b->size >= need + MIN_SPLIT) {
                mblock_t *tail = (mblock_t *)((char *)b + need);
                tail->size = b->size - need;
                tail->next = b->next;
                *pp        = tail;
                b->size    = need;
            } else {
                *pp = b->next;
            }
            return (char *)b + HDR_SIZE;
        }
    }

    /* No fit — grow the heap (in chunks to limit syscalls) */
    size_t chunk = need > GROW_CHUNK ? need : GROW_CHUNK;
    void *base = sbrk((int)chunk);
    if (base == (void *)-1) {
        if (chunk > need) {           /* retry with the exact amount */
            chunk = need;
            base  = sbrk((int)chunk);
        }
        if (base == (void *)-1) return (void *)0;
    }

    mblock_t *b = (mblock_t *)base;
    b->size = chunk;
    if (chunk >= need + MIN_SPLIT) {
        mblock_t *tail = (mblock_t *)((char *)b + need);
        tail->size = chunk - need;
        b->size    = need;
        insert_free(tail);
    }
    return (char *)b + HDR_SIZE;
}

void free(void *ptr) {
    if (!ptr) return;
    mblock_t *b = (mblock_t *)((char *)ptr - HDR_SIZE);
    insert_free(b);
}

void *realloc(void *ptr, size_t size) {
    if (!ptr)  return malloc(size);
    if (!size) { free(ptr); return (void *)0; }

    mblock_t *b = (mblock_t *)((char *)ptr - HDR_SIZE);
    size_t old_payload = b->size - HDR_SIZE;
    if (old_payload >= size)
        return ptr;                    /* shrink in place */

    void *np = malloc(size);
    if (!np) return (void *)0;
    char *src = (char *)ptr, *dst = (char *)np;
    for (size_t i = 0; i < old_payload; i++) dst[i] = src[i];
    free(ptr);
    return np;
}
