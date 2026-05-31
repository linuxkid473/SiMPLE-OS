#ifndef SIMPLE_VGA_H
#define SIMPLE_VGA_H

#include "types.h"

/* ---- Cell-buffer dimensions (must match internal vga.c arrays) ---- */
#define TERM_CELL_COLS  80
#define TERM_CELL_ROWS  60

/*
 * Per-terminal session snapshot.
 * The WM keeps one of these per STerm instance and swaps the global
 * vga state in/out when focus changes between terminals.
 */
typedef struct {
    char     cell_chars [TERM_CELL_ROWS][TERM_CELL_COLS];
    uint8_t  cell_colors[TERM_CELL_ROWS][TERM_CELL_COLS];
    uint32_t cursor_row, cursor_col;
    uint8_t  vga_color;
    uint32_t fb_cols, fb_rows;
    int32_t  draw_off_x, draw_off_y;
} term_session_t;

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
 * cols, rows   — client dimensions in character cells. */
void vga_set_client(int off_x, int off_y, uint32_t cols, uint32_t rows);

/* Redraw all cell-buffered text at the current draw offset. */
void vga_repaint_cells(void);

/* --- Multi-instance terminal session support --- */

/* Initialise s to a blank terminal state (spaces, default colour). */
void vga_init_session(term_session_t *s);

/* Copy the live global vga state into s. */
void vga_save_session(term_session_t *s);

/* Load s back into the live global vga state. */
void vga_restore_session(const term_session_t *s);

/* Repaint s's cell buffer directly to screen using s->draw_off_x/y.
 * Does NOT touch global state — safe to call for inactive windows. */
void vga_repaint_session(const term_session_t *s);

/* Fill a rectangle with a raw 32-bit RGB colour (desktop/chrome). */
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

/* Draw a string at pixel coords with raw RGB fg/bg (title bar text). */
void fb_draw_string_px(int x, int y, const char* s, uint32_t fg, uint32_t bg);

/* Blit a w*h array of 32bpp pixels (row-major) at screen position (x,y).
 * Clips to framebuffer bounds. Used by WM syscalls. */
void fb_blit_pixels(int x, int y, const uint32_t *src, int w, int h);

/* Nearest-neighbour scale-blit: src (src_w x src_h) stretched to
 * fill dst_w x dst_h at (x, y).  No extra buffer or malloc needed. */
void fb_blit_scaled(int x, int y, int dst_w, int dst_h,
                    const uint32_t *src, int src_w, int src_h);

/* Flush the back buffer to the real framebuffer (eliminates flicker).
 * Must be called once per fully-composed frame. */
void fb_flush(void);

#endif
