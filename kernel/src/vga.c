#include "vga.h"
#include "io.h"
#include "string.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;
static uint8_t vga_color = 0x0F;

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    if (cursor_row < VGA_HEIGHT) {
        return;
    }

    for (uint32_t y = 1; y < VGA_HEIGHT; y++) {
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (uint32_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    }

    cursor_row = VGA_HEIGHT - 1;
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void vga_clear(void) {
    for (uint32_t y = 0; y < VGA_HEIGHT; y++) {
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = VGA_WIDTH - 1;
        }
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(' ', vga_color);
    } else {
        VGA_MEMORY[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, vga_color);
        cursor_col++;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    }

    vga_scroll();
    vga_update_cursor();
}

void vga_write(const char* str) {
    while (*str) {
        vga_putc(*str++);
    }
}

void vga_write_line(const char* str) {
    vga_write(str);
    vga_putc('\n');
}

void vga_write_hex(uint32_t value) {
    static const char* hex = "0123456789ABCDEF";
    vga_write("0x");
    for (int i = 7; i >= 0; i--) {
        vga_putc(hex[(value >> (i * 4)) & 0xF]);
    }
}

uint16_t vga_get_cursor_pos(void) {
    return (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
}

void vga_set_cursor_pos(uint16_t pos) {
    if (pos >= (VGA_WIDTH * VGA_HEIGHT)) {
        pos = (VGA_WIDTH * VGA_HEIGHT) - 1;
    }
    cursor_row = (uint8_t)(pos / VGA_WIDTH);
    cursor_col = (uint8_t)(pos % VGA_WIDTH);
    vga_update_cursor();
}
