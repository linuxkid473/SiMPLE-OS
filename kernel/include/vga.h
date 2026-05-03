#ifndef SIMPLE_VGA_H
#define SIMPLE_VGA_H

#include "types.h"

void fb_init(uint32_t* addr, uint32_t width, uint32_t height, uint32_t pitch);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char* str);
void vga_write_line(const char* str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_write_hex(uint32_t value);
uint16_t vga_get_cursor_pos(void);
void vga_set_cursor_pos(uint16_t pos);

#endif
