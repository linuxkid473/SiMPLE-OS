#include "idt.h"
#include "registers.h"
#include "vga.h"

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

extern void isr21(void);

static void idt_set_gate(uint8_t num, uint32_t handler) {
    idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[num].selector = 0x08;
    idt[num].zero = 0;
    idt[num].flags = IDT_TYPE_INTERRUPT_GATE;
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void) {
    idt_set_gate(0x30, (uint32_t)isr21);
    idt_set_gate(0x22, (uint32_t)isr21);

    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idt_ptr.base = (uint32_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

void isr_handler(registers_t* regs) {
    vga_write("INT: ");
    uint32_t n = regs->int_no;
    if (n == 0) {
        vga_write("0\n");
    } else {
        char tmp[12];
        int i = 0;
        while (n > 0) {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
        while (i > 0) {
            vga_putc(tmp[--i]);
        }
        vga_putc('\n');
    }
}
