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
/* 4 KB page table for PDE[1] (0x400000–0x7FFFFF): replaces the old PSE entry
 * so individual heap pages can be marked user-accessible via SYS_SBRK. */
static uint32_t page_tab1[PT_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

/*
 * Physical page pool backing user heap allocations (SYS_SBRK).
 *
 * Lives at 0x900000–0x9FFFFF (1 MB, 256 pages).  This range sits inside
 * PDE[2] which is a 4 MB supervisor PSE page, so the kernel can always
 * write here.  User code reaches these frames only through the heap
 * mappings we install in page_tab1/proc_tab1 entries (PTE_USER set).
 */
#define PHYS_HEAP_BASE  0x900000U
#define PHYS_HEAP_LIMIT 0xA00000U   /* 1 MB = 256 × 4 KB pages */

static uint32_t phys_heap_next = PHYS_HEAP_BASE;

uint32_t paging_alloc_phys_page(void) {
    if (phys_heap_next >= PHYS_HEAP_LIMIT) return 0;
    uint32_t page = phys_heap_next;
    phys_heap_next += PAGE_SIZE;
    return page;
}

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

    /*
     * PDE[1]: 4 KB page table (page_tab1) instead of a PSE entry.
     *
     * Virtual 0x400000–0x4FFFFF (indices 0x000–0x0FF): heap region.
     * Left NOT PRESENT so SYS_SBRK can map them with PTE_USER on demand.
     *
     * Virtual 0x500000–0x7FFFFF (indices 0x100–0x3FF): child process slot
     * physical memory (PROC_POOL_BASE).  Identity-mapped supervisor-only so
     * proc_fork() can write the child image here.
     *
     * PDE itself carries U/S=1 so that PTE_USER heap entries are reachable
     * from ring-3 (both the PDE and PTE must have U/S=1 for user access).
     */
    for (uint32_t i = 0; i < 0x100U; i++)
        page_tab1[i] = 0;  /* heap range: not present until SYS_SBRK maps them */
    for (uint32_t i = 0x100U; i < PT_ENTRIES; i++)
        page_tab1[i] = (0x400000U + i * PAGE_SIZE) | PTE_PRESENT | PTE_RW;
    page_dir[1] = (uint32_t)page_tab1 | PDE_PRESENT | PDE_RW | PDE_USER;

    /* PD entries 2..1023: 4 MB identity-mapped supervisor pages. */
    for (uint32_t i = 2; i < PD_ENTRIES; i++) {
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

uint32_t *paging_get_page_dir(void) {
    return page_dir;
}

void paging_clone(uint32_t *dst_dir, uint32_t *dst_tab0, uint32_t *dst_tab1,
                  uint32_t phys_user_base) {
    /* PDE[0] page table: kernel + user image mappings */
    for (uint32_t i = 0; i < PT_ENTRIES; i++) {
        if (i == 0) {
            dst_tab0[i] = 0;  /* null guard */
        } else if (i < 0x300U) {
            /* kernel pages: identical physical frames, supervisor */
            dst_tab0[i] = (i * PAGE_SIZE) | PTE_PRESENT | PTE_RW;
        } else {
            /* user pages: remap to child's physical frames */
            dst_tab0[i] = (phys_user_base + (i - 0x300U) * PAGE_SIZE)
                          | PTE_PRESENT | PTE_RW | PTE_USER;
        }
    }

    /* PDE[0]: point to child's page table, U/S=1 */
    dst_dir[0] = (uint32_t)dst_tab0 | PDE_PRESENT | PDE_RW | PDE_USER;

    /*
     * PDE[1]: 4 KB page table for 0x400000–0x7FFFFF.
     * Heap range (0x400000–0x4FFFFF): not present — SYS_SBRK maps on demand.
     * Child-slot range (0x500000–0x7FFFFF): supervisor identity map preserved.
     */
    for (uint32_t i = 0; i < 0x100U; i++)
        dst_tab1[i] = 0;
    for (uint32_t i = 0x100U; i < PT_ENTRIES; i++)
        dst_tab1[i] = (0x400000U + i * PAGE_SIZE) | PTE_PRESENT | PTE_RW;
    dst_dir[1] = (uint32_t)dst_tab1 | PDE_PRESENT | PDE_RW | PDE_USER;

    /* PDE[2..1023]: 4 MB supervisor large pages */
    for (uint32_t i = 2; i < PD_ENTRIES; i++)
        dst_dir[i] = (i * LARGE_PAGE) | PDE_PRESENT | PDE_RW | PDE_PS;
}

void paging_switch_dir(uint32_t *dir) {
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)dir) : "memory");
}

int paging_page_mapped(uint32_t *page_dir, uint32_t vaddr) {
    uint32_t pde_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FFU;
    uint32_t pde = page_dir[pde_idx];
    if (!(pde & PDE_PRESENT) || (pde & PDE_PS)) return 0;
    uint32_t *ptab = (uint32_t *)(pde & ~0xFFFU);
    return (ptab[pte_idx] & PTE_PRESENT) ? 1 : 0;
}

void paging_map_page(uint32_t *page_dir, uint32_t vaddr, uint32_t paddr, int user) {
    uint32_t pde_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FFU;
    uint32_t pde = page_dir[pde_idx];
    if (!(pde & PDE_PRESENT) || (pde & PDE_PS)) {
        klog("paging", "map_page: PDE missing or PSE — cannot map");
        return;
    }
    uint32_t *ptab = (uint32_t *)(pde & ~0xFFFU);
    uint32_t flags = PTE_PRESENT | PTE_RW;
    if (user) flags |= PTE_USER;
    ptab[pte_idx] = (paddr & ~0xFFFU) | flags;
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}
