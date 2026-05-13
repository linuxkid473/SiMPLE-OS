#include "panic.h"
#include "klog.h"
#include "serial.h"
#include "vga.h"

void kernel_panic(const char* msg) {
    serial_write(COM1, "\n[SIMPLE] PANIC: ");
    if (msg) {
        serial_write(COM1, msg);
    }
    serial_write(COM1, "\n[SIMPLE] system halted\n");

    vga_set_color(0x0F, 0x04);
    vga_write_line("!!! KERNEL PANIC !!!");
    vga_set_color(0x0F, 0x01);
    if (msg) {
        vga_write("PANIC: ");
        vga_write_line(msg);
    } else {
        vga_write_line("PANIC: unknown");
    }
    vga_write_line("System halted.");

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void kernel_panic_regs(const char* msg, uint32_t int_no, uint32_t err_code,
                       uint32_t eip, uint32_t cs, uint32_t eflags) {
    serial_write(COM1, "\n[SIMPLE] PANIC: ");
    if (msg) {
        serial_write(COM1, msg);
    }
    serial_write(COM1, "\n");

    serial_write(COM1, "[SIMPLE] int_no=");
    serial_write_dec(COM1, int_no);
    serial_write(COM1, " err_code=");
    serial_write_hex(COM1, err_code);
    serial_write(COM1, " EIP=");
    serial_write_hex(COM1, eip);
    serial_write(COM1, " CS=");
    serial_write_hex(COM1, cs);
    serial_write(COM1, " EFLAGS=");
    serial_write_hex(COM1, eflags);
    serial_write(COM1, "\n[SIMPLE] system halted\n");

    vga_set_color(0x0F, 0x04);
    vga_write_line("!!! KERNEL PANIC !!!");
    vga_set_color(0x0F, 0x01);
    if (msg) {
        vga_write("PANIC: ");
        vga_write_line(msg);
    }
    vga_write("INT #");
    vga_write_hex(int_no);
    vga_write(" ERR=");
    vga_write_hex(err_code);
    vga_write(" EIP=");
    vga_write_hex(eip);
    vga_putc('\n');
    vga_write_line("System halted.");

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}
