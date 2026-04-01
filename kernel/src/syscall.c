#include "vga.h"

void sys_write(const char* buf, uint32_t len) {
    vga_write("<<<\n");
    for (uint32_t i = 0; i < len; i++) {
        vga_putc(buf[i]);
    }
    vga_write(">>>\n");
}
