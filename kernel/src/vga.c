#include "vga.h"
#include "io.h"
#include "string.h"
#include "font8x8_basic.h"

static uint32_t* fb_addr  = 0;
static uint32_t  fb_width  = 0;
static uint32_t  fb_height = 0;
static uint32_t  fb_pitch  = 0;

/*
 * fb_cols / fb_rows are the *effective* character grid dimensions.
 * After wm_init() they equal the client area size (e.g. 80 × 49),
 * not the full screen size.  All vga text operations respect these.
 */
static uint32_t fb_cols = 80;
static uint32_t fb_rows = 25;

/*
 * Pixel offset applied by draw_char_rgb when rendering a character
 * at grid position (col, row).  Set by vga_set_client() from wm.c.
 * In non-WM operation these stay at 0 and fb_cols/fb_rows cover
 * the full screen, so existing behaviour is preserved.
 */
static int32_t draw_off_x = 0;
static int32_t draw_off_y = 0;

/*
 * Cell backing buffer — stores every character and colour written via
 * draw_char() so the WM can repaint the terminal content after the
 * window is moved.  TERM_CELL_COLS/ROWS come from vga.h and are shared
 * with the term_session_t struct so sizes always agree.
 */
static char    cell_chars [TERM_CELL_ROWS][TERM_CELL_COLS];
static uint8_t cell_colors[TERM_CELL_ROWS][TERM_CELL_COLS];

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

static uint32_t cursor_row = 0;
static uint32_t cursor_col = 0;
static uint8_t  vga_color  = 0x0F;   /* white fg, black bg */

static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/* ------------------------------------------------------------------ */
/* Low-level pixel helpers                                             */
/* ------------------------------------------------------------------ */

/*
 * Draw one 8×8 character glyph at pixel coordinates (px, py) using
 * raw 32-bit RGB colours.  Clips to framebuffer bounds.
 * Does NOT touch the cell buffer — callers decide that.
 */
static void draw_char_rgb(char c, int px, int py, uint32_t fg, uint32_t bg) {
    if (!fb_addr) return;
    char *bitmap = font8x8_basic[(uint8_t)c];
    uint32_t stride = fb_pitch / 4;
    for (int r = 0; r < 8; r++) {
        int y = py + r;
        if (y < 0 || (uint32_t)y >= fb_height) continue;
        for (int cb = 0; cb < 8; cb++) {
            int x = px + cb;
            if (x < 0 || (uint32_t)x >= fb_width) continue;
            fb_addr[(uint32_t)y * stride + (uint32_t)x] =
                (bitmap[r] & (1 << cb)) ? fg : bg;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public initialisation                                               */
/* ------------------------------------------------------------------ */

void fb_init(uint32_t* addr, uint32_t width, uint32_t height, uint32_t pitch) {
    fb_addr   = addr;
    fb_width  = width;
    fb_height = height;
    fb_pitch  = pitch;
    fb_cols   = width  / 8;
    fb_rows   = height / 8;
    cursor_row = 0;
    cursor_col = 0;
    /* Pre-fill cell buffer with spaces so repaint shows a blank slate */
    memset(cell_chars,  ' ',  sizeof(cell_chars));
    memset(cell_colors, 0x0F, sizeof(cell_colors));
}

/* ------------------------------------------------------------------ */
/* WM integration — called by wm.c                                    */
/* ------------------------------------------------------------------ */

/*
 * Redirect all vga text operations into the window client area.
 * off_x, off_y — top-left pixel of the client area on screen.
 * cols, rows   — client area size in character cells.
 * After this call fb_cols/fb_rows reflect the client dimensions, so
 * all existing vga scroll / cursor / clear logic naturally limits
 * itself to the window without further changes.
 */
void vga_set_client(int off_x, int off_y, uint32_t cols, uint32_t rows) {
    draw_off_x = (int32_t)off_x;
    draw_off_y = (int32_t)off_y;
    fb_cols    = cols;
    fb_rows    = rows;
}

/*
 * Replay the cell buffer at the current draw offset.
 * Called by wm_handle_key() after updating draw_off_x/y so that
 * existing terminal content reappears inside the window at its new
 * screen position.
 */
void vga_repaint_cells(void) {
    if (!fb_addr) return;
    for (uint32_t r = 0; r < fb_rows && r < TERM_CELL_ROWS; r++) {
        for (uint32_t c = 0; c < fb_cols && c < TERM_CELL_COLS; c++) {
            uint8_t col = cell_colors[r][c];
            draw_char_rgb(
                cell_chars[r][c],
                (int)(draw_off_x + (int32_t)(c * 8)),
                (int)(draw_off_y + (int32_t)(r * 8)),
                vga_palette[col & 0x0F],
                vga_palette[(col >> 4) & 0x0F]
            );
        }
    }
}

/*
 * Fill a rectangle on the raw framebuffer with a 32-bit RGB colour.
 * Used by wm.c to draw the desktop background, borders, and title bar.
 * Clips to framebuffer bounds.
 */
void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_addr) return;
    uint32_t stride = fb_pitch / 4;
    for (int ry = y; ry < y + h; ry++) {
        if (ry < 0 || (uint32_t)ry >= fb_height) continue;
        for (int rx = x; rx < x + w; rx++) {
            if (rx < 0 || (uint32_t)rx >= fb_width) continue;
            fb_addr[(uint32_t)ry * stride + (uint32_t)rx] = color;
        }
    }
}

/* Blit a row-major w*h 32bpp pixel array to (x,y) on the framebuffer. */
void fb_blit_pixels(int x, int y, const uint32_t *src, int w, int h) {
    if (!fb_addr || !src || w <= 0 || h <= 0) return;
    uint32_t stride = fb_pitch / 4;
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || (uint32_t)dy >= fb_height) continue;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || (uint32_t)dx >= fb_width) continue;
            fb_addr[(uint32_t)dy * stride + (uint32_t)dx] = src[row * w + col];
        }
    }
}

/*
 * Draw a string at a raw pixel position with explicit RGB fg/bg.
 * Used by wm.c to render the title bar text outside the cell grid.
 */
void fb_draw_string_px(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    if (!fb_addr) return;
    int cx = x;
    while (*s) {
        draw_char_rgb(*s, cx, y, fg, bg);
        cx += 8;
        s++;
    }
}

/* ------------------------------------------------------------------ */
/* Core character drawing — VGA text mode + framebuffer               */
/* ------------------------------------------------------------------ */

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_update_cursor(void) {
    if (fb_addr) return;   /* hardware cursor only used in VGA text mode */
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/*
 * Draw one character at (col, row) in the logical grid.
 * In framebuffer mode the pixel position is (draw_off_x + col*8,
 * draw_off_y + row*8), so text lands inside the window client area.
 * The character is also stored in the cell buffer so wm_repaint_cells
 * can reconstruct it later.
 */
static void draw_char(char c, uint32_t col, uint32_t row, uint8_t color) {
    if (!fb_addr) {
        if (row < VGA_HEIGHT && col < VGA_WIDTH) {
            VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, color);
        }
        return;
    }
    /* Store to cell buffer for later repaint */
    if (row < TERM_CELL_ROWS && col < TERM_CELL_COLS) {
        cell_chars [row][col] = c;
        cell_colors[row][col] = color;
    }
    draw_char_rgb(
        c,
        (int)(draw_off_x + (int32_t)(col * 8)),
        (int)(draw_off_y + (int32_t)(row * 8)),
        vga_palette[color & 0x0F],
        vga_palette[(color >> 4) & 0x0F]
    );
}

static void vga_scroll(void) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;

    if (cursor_row < max_rows) return;

    if (fb_addr) {
        /*
         * Pixel blit: shift the client area up by exactly one
         * character row (8 pixels), then clear the last row.
         * Only touches pixels within [draw_off_x .. draw_off_x + cw)
         * so the window chrome and desktop are left intact.
         */
        uint32_t stride = fb_pitch / 4;
        uint32_t ox = (uint32_t)(draw_off_x < 0 ? 0 : draw_off_x);
        uint32_t oy = (uint32_t)(draw_off_y < 0 ? 0 : draw_off_y);
        uint32_t cw = max_cols * 8;
        uint32_t ch = max_rows * 8;

        for (uint32_t py = oy + 8; py < oy + ch; py++) {
            for (uint32_t px = ox; px < ox + cw; px++) {
                fb_addr[(py - 8) * stride + px] = fb_addr[py * stride + px];
            }
        }
        uint32_t bg = vga_palette[(vga_color >> 4) & 0x0F];
        for (uint32_t py = oy + ch - 8; py < oy + ch; py++) {
            for (uint32_t px = ox; px < ox + cw; px++) {
                fb_addr[py * stride + px] = bg;
            }
        }

        /* Mirror the scroll in the cell buffer */
        for (uint32_t r = 0; r + 1 < max_rows && r + 1 < TERM_CELL_ROWS; r++) {
            for (uint32_t c = 0; c < max_cols && c < TERM_CELL_COLS; c++) {
                cell_chars [r][c] = cell_chars [r + 1][c];
                cell_colors[r][c] = cell_colors[r + 1][c];
            }
        }
        for (uint32_t c = 0; c < max_cols && c < TERM_CELL_COLS; c++) {
            cell_chars [max_rows - 1][c] = ' ';
            cell_colors[max_rows - 1][c] = vga_color;
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

/* ------------------------------------------------------------------ */
/* Multi-instance terminal session support                            */
/* ------------------------------------------------------------------ */

void vga_init_session(term_session_t *s) {
    uint32_t r, c;
    for (r = 0; r < TERM_CELL_ROWS; r++)
        for (c = 0; c < TERM_CELL_COLS; c++) {
            s->cell_chars [r][c] = ' ';
            s->cell_colors[r][c] = 0x0F;
        }
    s->cursor_row = 0;
    s->cursor_col = 0;
    s->vga_color  = 0x0F;
    s->fb_cols    = 80;
    s->fb_rows    = 49;    /* matches terminal window client height / 8 */
    s->draw_off_x = 0;
    s->draw_off_y = 0;
}

void vga_save_session(term_session_t *s) {
    memcpy(s->cell_chars,  cell_chars,  sizeof(cell_chars));
    memcpy(s->cell_colors, cell_colors, sizeof(cell_colors));
    s->cursor_row = cursor_row;
    s->cursor_col = cursor_col;
    s->vga_color  = vga_color;
    s->fb_cols    = fb_cols;
    s->fb_rows    = fb_rows;
    s->draw_off_x = draw_off_x;
    s->draw_off_y = draw_off_y;
}

void vga_restore_session(const term_session_t *s) {
    memcpy(cell_chars,  s->cell_chars,  sizeof(cell_chars));
    memcpy(cell_colors, s->cell_colors, sizeof(cell_colors));
    cursor_row = s->cursor_row;
    cursor_col = s->cursor_col;
    vga_color  = s->vga_color;
    fb_cols    = s->fb_cols;
    fb_rows    = s->fb_rows;
    draw_off_x = s->draw_off_x;
    draw_off_y = s->draw_off_y;
}

/*
 * Render s's cell buffer directly to screen at s->draw_off_x/y.
 * Leaves global state untouched — used to paint inactive terminal windows.
 */
void vga_repaint_session(const term_session_t *s) {
    if (!fb_addr) return;
    for (uint32_t r = 0; r < s->fb_rows && r < TERM_CELL_ROWS; r++) {
        for (uint32_t c = 0; c < s->fb_cols && c < TERM_CELL_COLS; c++) {
            uint8_t col = s->cell_colors[r][c];
            draw_char_rgb(
                s->cell_chars[r][c],
                s->draw_off_x + (int)(c * 8),
                s->draw_off_y + (int)(r * 8),
                vga_palette[col & 0x0F],
                vga_palette[(col >> 4) & 0x0F]
            );
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public vga API (unchanged interface, updated internals)            */
/* ------------------------------------------------------------------ */

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
    while (*str) vga_putc(*str++);
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
    if (pos >= (uint16_t)(max_cols * max_rows)) {
        pos = (uint16_t)(max_cols * max_rows - 1);
    }
    cursor_row = pos / max_cols;
    cursor_col = pos % max_cols;
    vga_update_cursor();
}
