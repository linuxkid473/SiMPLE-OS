/*
 * pic.c — 8259A Programmable Interrupt Controller driver.
 *
 * The BIOS leaves the PIC in its default state:
 *   IRQ0-7  → INT 0x08-0x0F   ← conflicts with CPU exception vectors!
 *   IRQ8-15 → INT 0x70-0x77
 *
 * pic_init() remaps both PICs so hardware IRQs land on vectors 0x20-0x2F,
 * safely above the 32 reserved CPU exception vectors.  All IRQ lines are
 * masked afterward; callers enable what they need with pic_unmask().
 */

#include "io.h"
#include "klog.h"
#include "pic.h"
#include "types.h"

#define ICW1_INIT 0x11   /* begin initialization, ICW4 required */
#define ICW4_8086 0x01   /* 8086/88 mode (not MCS-80/85) */

void pic_init(void) {
    /* ICW1: start initialization sequence (cascade mode, edge triggered) */
    outb(PIC1_CMD,  ICW1_INIT);  io_wait();
    outb(PIC2_CMD,  ICW1_INIT);  io_wait();

    /* ICW2: interrupt vector base offsets */
    outb(PIC1_DATA, PIC1_OFFSET);  io_wait();   /* master: IRQ0-7 → 0x20-0x27 */
    outb(PIC2_DATA, PIC2_OFFSET);  io_wait();   /* slave:  IRQ8-15 → 0x28-0x2F */

    /* ICW3: cascading wiring */
    outb(PIC1_DATA, 0x04);  io_wait();  /* master: slave on IRQ2 (bit mask) */
    outb(PIC2_DATA, 0x02);  io_wait();  /* slave:  cascade identity = IRQ2   */

    /* ICW4: 8086 mode, normal EOI */
    outb(PIC1_DATA, ICW4_8086);  io_wait();
    outb(PIC2_DATA, ICW4_8086);  io_wait();

    /* Mask all IRQ lines — callers enable selectively */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    klog("pic", "8259 remapped: IRQ0-7→0x20, IRQ8-15→0x28, all masked");
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);  /* slave needs EOI for IRQ8-15 */
    outb(PIC1_CMD, 0x20);      /* master always needs EOI */
}

void pic_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ?  irq      : (uint8_t)(irq - 8);
    outb(port, inb(port) | (uint8_t)(1U << bit));
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ?  irq      : (uint8_t)(irq - 8);
    outb(port, inb(port) & (uint8_t)(~(1U << bit)));
}
