#ifndef SIMPLE_PIC_H
#define SIMPLE_PIC_H

#include "types.h"

/* 8259A Programmable Interrupt Controller port addresses */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

/*
 * After pic_init() the 8259 is remapped so hardware IRQ lines land on
 * IDT vectors that don't conflict with CPU exception vectors 0-31.
 *   IRQ0-7  → INT 0x20-0x27   (master PIC)
 *   IRQ8-15 → INT 0x28-0x2F   (slave PIC)
 */
#define PIC1_OFFSET 0x20
#define PIC2_OFFSET 0x28

/*
 * Remap both PICs, then mask every IRQ line.
 * Call this before idt_init() and before sti.
 */
void pic_init(void);

/*
 * Send End-Of-Interrupt for the given hardware IRQ number (0-15).
 * Must be called before returning from every IRQ handler so the PIC
 * can assert new interrupts.
 */
void pic_eoi(uint8_t irq);

/* Mask / unmask a single IRQ line (0-15). */
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

#endif
