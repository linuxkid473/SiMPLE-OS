#ifndef SIMPLE_PAGING_H
#define SIMPLE_PAGING_H

#include "types.h"

void      paging_init(void);

/* Returns the kernel's page directory (set up by paging_init). */
uint32_t *paging_get_page_dir(void);

/*
 * Build a new page directory for a child process.
 *   dst_dir       — 1024-entry page directory (page-aligned)
 *   dst_tab0      — 1024-entry page table for first 4 MB (page-aligned)
 *   dst_tab1      — 1024-entry page table for second 4 MB (0x400000-0x7FFFFF)
 *   phys_user_base — physical address of the child's 1 MB user image
 *
 * Kernel pages (0x000000-0x2FFFFF) share the same physical frames as the
 * parent; user pages (0x300000-0x3FFFFF) are remapped to phys_user_base.
 * The second 4 MB (0x400000-0x7FFFFF) is identity-mapped supervisor so the
 * child can grow its heap via SYS_SBRK after fork.
 */
void      paging_clone(uint32_t *dst_dir, uint32_t *dst_tab0, uint32_t *dst_tab1,
                       uint32_t phys_user_base);

/* Load a page directory into CR3 (TLB flush). */
void      paging_switch_dir(uint32_t *page_dir);

/* Allocate one physical 4 KB page from the heap backing pool.
 * Returns the physical address, or 0 if the pool is exhausted. */
uint32_t  paging_alloc_phys_page(void);

/* Map a single 4 KB page in page_dir: vaddr → paddr.
 * user=1 sets U/S so ring-3 code can access it; user=0 is supervisor-only.
 * The target PDE must already be a 4 KB page table (not PSE). */
void      paging_map_page(uint32_t *page_dir, uint32_t vaddr,
                          uint32_t paddr, int user);

/* Return 1 if the 4 KB page covering vaddr is already present in page_dir. */
int       paging_page_mapped(uint32_t *page_dir, uint32_t vaddr);

#endif
