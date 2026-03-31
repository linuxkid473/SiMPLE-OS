#ifndef SIMPLE_IDT_H
#define SIMPLE_IDT_H

#include "types.h"

#define IDT_ENTRIES 256

#define IDT_FLAG_PRESENT    0x80
#define IDT_FLAG_DPL0       0x00
#define IDT_FLAG_DPL1       0x20
#define IDT_FLAG_DPL2       0x40
#define IDT_FLAG_DPL3       0x60
#define IDT_FLAG_32BIT      0x08
#define IDT_FLAG_INTERRUPT  0x06
#define IDT_FLAG_TRAP       0x07

#define IDT_TYPE_INTERRUPT_GATE (IDT_FLAG_PRESENT | IDT_FLAG_32BIT | IDT_FLAG_INTERRUPT)

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void idt_init(void);

#endif
