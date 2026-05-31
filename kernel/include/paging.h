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

/*
 * Mark the 4 MB PSE page covering phys_addr as Uncacheable (PWT+PCD).
 * Call this after obtaining an MMIO BAR address to prevent the CPU from
 * caching MMIO register reads/writes.  Performs a full TLB flush.
 */
void      paging_mark_uc(uint32_t phys_addr);

/*
 * Print all user-accessible virtual→physical mappings in pdir to COM1.
 * tag is a short label printed in the header line.
 */
void      paging_dump_map(uint32_t *pdir, const char *tag);

/*
 * Return the number of user physical pages shared between dir_a and dir_b.
 * 0 means the two address spaces are fully isolated.  Logs to COM1.
 */
int       paging_verify_isolation(uint32_t *dir_a, uint32_t *dir_b,
                                   const char *na, const char *nb);

/*
 * Reset the physical page pool and clear all heap-range PTEs.
 *
 * Must be called by proc_register_initial() before loading a new ELF so that:
 *   (a) paging_alloc_phys_page() starts fresh — no exhaustion from a previous
 *       run's fork() calls.
 *   (b) page_tab1[0..0xFF] (virtual 0x400000–0x4FFFFF) is fully cleared —
 *       no stale present-bit entries that would fool paging_page_mapped()
 *       into thinking the new process's heap is already mapped.
 */
void      paging_reset_phys_heap(void);

/*
 * Per-slot physical memory for spawned user processes.
 *
 * Each process slot 1-7 gets a dedicated 1 MB identity-mapped physical
 * region above the phys-heap pool (0x900000-0x9FFFFF).  These addresses
 * live inside PDE[2..3] supervisor 4 MB PSE pages and are always writable
 * by the kernel as identity-mapped supervisor memory (physical == virtual).
 *
 * Slot  1: 0xA00000-0xAFFFFF
 * Slot  2: 0xB00000-0xBFFFFF
 * ...
 * Slot  7: 0x1000000-0x10FFFFF
 */
#define PROC_SLOT_PHYS(n)   (0xA00000U + ((uint32_t)((n) - 1) * 0x100000U))
#define PROC_SLOT_SIZE      0x100000U   /* 1 MB per slot */

#endif
