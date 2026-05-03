#include "vga.h"
#include "io.h"
#include "string.h"
#include "font8x8_basic.h"

static uint32_t* fb_addr = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;

static uint32_t fb_cols = 80;
static uint32_t fb_rows = 25;

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

static uint32_t cursor_row = 0;
static uint32_t cursor_col = 0;
static uint8_t vga_color = 0x0F;

static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

void fb_init(uint32_t* addr, uint32_t width, uint32_t height, uint32_t pitch) {
    fb_addr = addr;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
    fb_cols = width / 8;
    fb_rows = height / 8;
    cursor_row = 0;
    cursor_col = 0;
}

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_update_cursor(void) {
    if (fb_addr) return;
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void draw_char(char c, uint32_t col, uint32_t row, uint8_t color) {
    if (!fb_addr) {
        if (row < VGA_HEIGHT && col < VGA_WIDTH) {
            VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, color);
        }
        return;
    }

    uint32_t fg = vga_palette[color & 0x0F];
    uint32_t bg = vga_palette[(color >> 4) & 0x0F];
    char *bitmap = font8x8_basic[(uint8_t)c];
    uint32_t x = col * 8;
    uint32_t y = row * 8;

    for (int r = 0; r < 8; r++) {
        for (int c_bit = 0; c_bit < 8; c_bit++) {
            uint32_t px_color = (bitmap[r] & (1 << c_bit)) ? fg : bg;
            if (y + r < fb_height && x + c_bit < fb_width) {
                fb_addr[(y + r) * (fb_pitch / 4) + (x + c_bit)] = px_color;
            }
        }
    }
}

static void vga_scroll(void) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;

    if (cursor_row < max_rows) {
        return;
    }

    if (fb_addr) {
        uint32_t row_bytes = max_cols * 8 * 4;
        for (uint32_t y = 8; y < fb_height; y++) {
            for (uint32_t x = 0; x < fb_width; x++) {
                fb_addr[(y - 8) * (fb_pitch / 4) + x] = fb_addr[y * (fb_pitch / 4) + x];
            }
        }
        for (uint32_t y = fb_height - 8; y < fb_height; y++) {
            for (uint32_t x = 0; x < fb_width; x++) {
                fb_addr[y * (fb_pitch / 4) + x] = vga_palette[(vga_color >> 4) & 0x0F];
            }
        }
    } else {
        for (uint32_t y = 1; y < VGA_HEIGHT; y++) {
            for (uint32_t x = 0; x < VGA_WIDTH; x++) {
                VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
            }
        }
        for (uint32_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        }
    }

    cursor_row = max_rows - 1;
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void vga_clear(void) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;

    for (uint32_t y = 0; y < max_rows; y++) {
        for (uint32_t x = 0; x < max_cols; x++) {
            draw_char(' ', x, y, vga_color);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
}

void vga_putc(char c) {
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;

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
            cursor_col = max_cols - 1;
        }
        draw_char(' ', cursor_col, cursor_row, vga_color);
    } else {
        draw_char(c, cursor_col, cursor_row, vga_color);
        cursor_col++;
        if (cursor_col >= max_cols) {
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
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;
    return (uint16_t)(cursor_row * max_cols + cursor_col);
}

void vga_set_cursor_pos(uint16_t pos) {
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    
    if (pos >= (max_cols * max_rows)) {
        pos = (max_cols * max_rows) - 1;
    }
    cursor_row = pos / max_cols;
    cursor_col = pos % max_cols;
    vga_update_cursor();
}
