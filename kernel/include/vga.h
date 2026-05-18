#ifndef SIMPLE_VGA_H
#define SIMPLE_VGA_H

#include "types.h"

/* ---- Framebuffer init ---- */
void fb_init(uint32_t* addr, uint32_t width, uint32_t height, uint32_t pitch);

/* ---- Basic text output ---- */
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char* str);
void vga_write_line(const char* str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_write_hex(uint32_t value);
uint16_t vga_get_cursor_pos(void);
void vga_set_cursor_pos(uint16_t pos);

/* ---- WM / window support ---- */

/* Restrict vga text operations to a window client area.
 * off_x, off_y — top-left pixel of the client area on screen.
 * cols, rows   — client dimensions in character cells.
 * Called from wm.c on init and whenever the window moves. */
void vga_set_client(int off_x, int off_y, uint32_t cols, uint32_t rows);

/* Redraw all cell-buffered text at the current draw offset.
 * Called by wm_handle_key() after moving the window. */
void vga_repaint_cells(void);

/* Fill a rectangle with a raw 32-bit RGB colour (desktop/chrome). */
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Draw a string at pixel coords with raw RGB fg/bg (title bar text). */
void fb_draw_string_px(int x, int y, const char* s, uint32_t fg, uint32_t bg);

#endif
