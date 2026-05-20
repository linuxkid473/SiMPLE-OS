#include "gdt.h"
#include "types.h"

#define GDT_ENTRIES 6

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct {
    uint32_t link;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldtr;
    uint16_t trap;
    uint16_t iopb;
} __attribute__((packed)) tss_t;

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gp;
static tss_t            tss;

extern void gdt_flush(uint32_t);
extern void tss_flush(void);

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran) {
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access    = access;
}

static void tss_install(int i, uint32_t base, uint32_t limit) {
    gdt[i].limit_low  = limit & 0xFFFF;
    gdt[i].base_low   = base & 0xFFFF;
    gdt[i].base_mid   = (base >> 16) & 0xFF;
    gdt[i].access     = 0x89;  /* present, DPL=0, TSS 32-bit available */
    gdt[i].gran       = 0x00;
    gdt[i].base_high  = (base >> 24) & 0xFF;
}

void tss_set_esp0(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    /* zero TSS */
    uint8_t *tp = (uint8_t *)&tss;
    for (uint32_t i = 0; i < sizeof(tss_t); i++) tp[i] = 0;

    tss.ss0  = SEG_KDATA;
    tss.esp0 = 0;              /* set via tss_set_esp0() before ring3 launch */
    tss.iopb = sizeof(tss_t); /* no I/O permission bitmap */

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint32_t)&gdt;

    gdt_set(0, 0, 0,           0x00, 0x00); /* null */
    gdt_set(1, 0, 0xFFFFFFFF,  0x9A, 0xCF); /* kernel code  DPL=0 */
    gdt_set(2, 0, 0xFFFFFFFF,  0x92, 0xCF); /* kernel data  DPL=0 */
    gdt_set(3, 0, 0xFFFFFFFF,  0xFA, 0xCF); /* user code    DPL=3 */
    gdt_set(4, 0, 0xFFFFFFFF,  0xF2, 0xCF); /* user data    DPL=3 */
    tss_install(5, (uint32_t)&tss, sizeof(tss_t) - 1);

    gdt_flush((uint32_t)&gp);
    tss_flush();
}
