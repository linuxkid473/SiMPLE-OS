#include "panic.h"
#include "klog.h"
#include "registers.h"
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

void kernel_panic_full(const char* msg, registers_t* regs) {
    serial_write(COM1, "\n[SIMPLE] PANIC: ");
    if (msg) serial_write(COM1, msg);
    serial_write(COM1, "\n");

    if (regs) {
        serial_write(COM1, "[SIMPLE] INT=");  serial_write_dec(COM1, regs->int_no);
        serial_write(COM1, " ERR=");          serial_write_hex(COM1, regs->err_code);
        serial_write(COM1, " EIP=");          serial_write_hex(COM1, regs->eip);
        serial_write(COM1, " CS=");           serial_write_hex(COM1, regs->cs);
        serial_write(COM1, " EFLAGS=");       serial_write_hex(COM1, regs->eflags);
        serial_write(COM1, "\n");
        serial_write(COM1, "[SIMPLE] EAX=");  serial_write_hex(COM1, regs->eax);
        serial_write(COM1, " EBX=");          serial_write_hex(COM1, regs->ebx);
        serial_write(COM1, " ECX=");          serial_write_hex(COM1, regs->ecx);
        serial_write(COM1, " EDX=");          serial_write_hex(COM1, regs->edx);
        serial_write(COM1, "\n");
        serial_write(COM1, "[SIMPLE] ESP=");  serial_write_hex(COM1, regs->esp);
        serial_write(COM1, " EBP=");          serial_write_hex(COM1, regs->ebp);
        serial_write(COM1, " ESI=");          serial_write_hex(COM1, regs->esi);
        serial_write(COM1, " EDI=");          serial_write_hex(COM1, regs->edi);
        serial_write(COM1, "\n");
    }
    serial_write(COM1, "[SIMPLE] system halted\n");

    vga_set_color(0x0F, 0x04);
    vga_write_line("!!! KERNEL PANIC !!!");
    vga_set_color(0x0F, 0x01);
    if (msg) { vga_write("PANIC: "); vga_write_line(msg); }
    if (regs) {
        vga_write("INT #"); vga_write_hex(regs->int_no);
        vga_write(" ERR="); vga_write_hex(regs->err_code);
        vga_write(" EIP="); vga_write_hex(regs->eip);
        vga_putc('\n');
        vga_write("EAX="); vga_write_hex(regs->eax);
        vga_write(" EBX="); vga_write_hex(regs->ebx);
        vga_write(" ECX="); vga_write_hex(regs->ecx);
        vga_write(" EDX="); vga_write_hex(regs->edx);
        vga_putc('\n');
        vga_write("ESP="); vga_write_hex(regs->esp);
        vga_write(" EBP="); vga_write_hex(regs->ebp);
        vga_putc('\n');
    }
    vga_write_line("System halted.");

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}
