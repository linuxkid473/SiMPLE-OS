#include "paging.h"
#include "klog.h"
#include "types.h"

/*
 * Simple identity-mapped page tables with ring3 protection.
 *
 * Layout (physical == virtual, identity map):
 *
 *   PD entry 0  (0x000000..0x3FFFFF, 4KB page table):
 *     Page 0x000              : NOT present  (null guard)
 *     Pages 0x001..0x2FF      : supervisor   (kernel code/data/heap)
 *     Pages 0x300..0x3FF      : USER         (user ELF + stack)
 *
 *   PD entries 1..1023 (4MB PSE pages, supervisor):
 *     0x400000..0xFFFFFFFF    : supervisor   (framebuffer, MMIO, etc.)
 *
 * Both the PD entry and PT entry must have U/S=1 for user access; PT entries
 * for kernel pages within PD-entry-0 have U/S=0 so they remain supervisor-only
 * even though the PDE itself carries U/S=1 (needed for the user pages within).
 */

#define PAGE_SIZE   0x1000U
#define LARGE_PAGE  0x400000U

#define PDE_PRESENT (1U << 0)
#define PDE_RW      (1U << 1)
#define PDE_USER    (1U << 2)
#define PDE_PS      (1U << 7)   /* 4 MB page size */

#define PTE_PRESENT (1U << 0)
#define PTE_RW      (1U << 1)
#define PTE_USER    (1U << 2)

/* User region: 0x300000 .. 0x3FFFFF  (must match elf.c USER_BASE / USER_STACK) */
#define USER_PT_START  0x300U   /* first user page index in PT0  */
#define USER_PT_END    0x400U   /* exclusive                     */

#define PT_ENTRIES  1024U
#define PD_ENTRIES  1024U

static uint32_t page_dir[PD_ENTRIES]  __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_tab0[PT_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

void paging_init(void) {
    /* Build 4KB page table for the first 4MB (PD entry 0). */
    for (uint32_t i = 0; i < PT_ENTRIES; i++) {
        if (i == 0) {
            page_tab0[i] = 0; /* null guard: not present */
            continue;
        }
        uint32_t flags = PTE_PRESENT | PTE_RW;
        if (i >= USER_PT_START && i < USER_PT_END)
            flags |= PTE_USER;
        page_tab0[i] = (i * PAGE_SIZE) | flags;
    }

    /*
     * PD entry 0: points to page_tab0.
     * Must carry U/S=1 so that the user PT entries within are reachable.
     * Kernel PT entries inside it still have U/S=0 → supervisor-only.
     */
    page_dir[0] = (uint32_t)page_tab0 | PDE_PRESENT | PDE_RW | PDE_USER;

    /* Enable PSE (4 MB pages) in CR4. */
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1U << 4);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");

    /* PD entries 1..1023: 4 MB identity-mapped supervisor pages. */
    for (uint32_t i = 1; i < PD_ENTRIES; i++) {
        page_dir[i] = (i * LARGE_PAGE) | PDE_PRESENT | PDE_RW | PDE_PS;
        /* U/S=0  →  supervisor-only */
    }

    /* Install new page directory and flush TLB. */
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_dir) : "memory");

    /* Ensure PG bit in CR0 is set (stivale2 enables paging, but be explicit). */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1U << 31);
    __asm__ volatile("mov %0, %%cr0\n\t"
                     "jmp 1f\n\t"  /* flush prefetch queue */
                     "1:\n\t"
                     : : "r"(cr0) : "memory");

    klog("paging", "ring3 page tables installed");
    klog_hex("paging", "page_dir phys", (uint32_t)page_dir);
}
