#ifndef SIMPLE_GDT_H
#define SIMPLE_GDT_H

#include "types.h"

/* Segment selectors */
#define SEG_KCODE  0x08          /* kernel code,  DPL=0          */
#define SEG_KDATA  0x10          /* kernel data,  DPL=0          */
#define SEG_UCODE  0x1B          /* user code,  0x18 | RPL=3     */
#define SEG_UDATA  0x23          /* user data,  0x20 | RPL=3     */
#define SEG_TSS    0x28          /* TSS descriptor               */

void gdt_init(void);
void tss_set_esp0(uint32_t esp0);

#endif
