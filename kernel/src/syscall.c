#include "vga.h"
#include "types.h"
#include "registers.h"

#define SYS_WRITE 1
#define SYS_EXIT  2

void sys_write(const char* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        vga_putc(buf[i]);
    }
}

void syscall_handler(registers_t* regs) {
    switch (regs->eax) {

        case SYS_WRITE:
            sys_write((const char*)regs->ecx, regs->edx);
            break;

        case SYS_EXIT:
            while (1) {
                __asm__ volatile("hlt");
            }
            break;

        default:
            vga_write_line("unknown syscall");
            break;
    }
}