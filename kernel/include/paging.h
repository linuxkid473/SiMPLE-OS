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
 *   phys_user_base — physical address of the child's 1 MB user image
 *
 * Kernel pages (0x000000-0x2FFFFF) share the same physical frames as the
 * parent; user pages (0x300000-0x3FFFFF) are remapped to phys_user_base.
 */
void      paging_clone(uint32_t *dst_dir, uint32_t *dst_tab0,
                       uint32_t phys_user_base);

/* Load a page directory into CR3 (TLB flush). */
void      paging_switch_dir(uint32_t *page_dir);

#endif
