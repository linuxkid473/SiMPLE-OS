#include "vga.h"
#include "io.h"
#include "string.h"
#include "font8x8_basic.h"

static uint32_t* fb_addr  = 0;
static uint32_t  fb_width  = 0;
static uint32_t  fb_height = 0;
static uint32_t  fb_pitch  = 0;

/* Back buffer — all drawing goes here; fb_flush() blits to the real
 * framebuffer in one shot so the display only ever shows complete frames.
 *
 * Lives at a fixed address in the supervisor PSE region (PDE[2], identity-
 * mapped 4 MB page starting at 0x800000). Placed just above the physical
 * page pool which ends at 0x9FFFFF, runs to ~0xBD4BFF (< 0xC00000). Using
 * a pointer rather than a static array keeps BSS below 0x400000 so it
 * stays within the pages that paging_init() maps present. */
/* Process slots occupy 0xA00000–0x10FFFFF (7 slots × 1 MB).
 * Place the back buffer after them at 0x1200000, inside PDE[4] which is
 * a supervisor 4 MB PSE page (0x1000000–0x13FFFFF). */
#define FB_BACK_PHYS 0x1200000U
static uint32_t * const fb_back = (uint32_t *)FB_BACK_PHYS;

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

/* ANSI / VT100 escape state machine */
typedef enum { ANSI_NORMAL, ANSI_ESC, ANSI_CSI } ansi_state_t;
#define ANSI_MAX_PARAMS 8
static ansi_state_t ansi_state = ANSI_NORMAL;
static int          ansi_params[ANSI_MAX_PARAMS];
static int          ansi_nparams = 0;
static int          ansi_priv = 0;   /* '?' flag seen after CSI */

/* DECSTBM scroll region, 0-based inclusive rows.
 * scroll_bot = 0xFFFFFFFF means "bottom of screen" (region unset). */
static uint32_t scroll_top = 0;
static uint32_t scroll_bot = 0xFFFFFFFFu;

/* CSI s / CSI u saved cursor */
static uint32_t saved_cur_row = 0;
static uint32_t saved_cur_col = 0;

/* Software cursor (framebuffer mode): inverse-video cell at the cursor.
 * Toggled by DECTCEM (CSI ?25h / ?25l). */
static int      cursor_visible = 1;
static int      cursor_drawn   = 0;
static uint32_t cdrawn_row = 0, cdrawn_col = 0;

/* ANSI color index → VGA CGA palette index */
static const uint8_t ansi_to_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

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
            uint32_t col = (bitmap[r] & (1 << cb)) ? fg : bg;
            fb_back[(uint32_t)y * fb_width + (uint32_t)x] = col;
            fb_addr[(uint32_t)y * stride    + (uint32_t)x] = col;
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
    for (int ry = y; ry < y + h; ry++) {
        if (ry < 0 || (uint32_t)ry >= fb_height) continue;
        for (int rx = x; rx < x + w; rx++) {
            if (rx < 0 || (uint32_t)rx >= fb_width) continue;
            fb_back[(uint32_t)ry * fb_width + (uint32_t)rx] = color;
        }
    }
}

/* Blit a row-major w*h 32bpp pixel array to (x,y) on the framebuffer. */
void fb_blit_pixels(int x, int y, const uint32_t *src, int w, int h) {
    if (!fb_addr || !src || w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || (uint32_t)dy >= fb_height) continue;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || (uint32_t)dx >= fb_width) continue;
            fb_back[(uint32_t)dy * fb_width + (uint32_t)dx] = src[row * w + col];
        }
    }
}

/* Blit src (src_w x src_h) scaled to fill dst_w x dst_h at (x,y).
 * Nearest-neighbour.  Precomputes source-X table to avoid per-pixel division. */
void fb_blit_scaled(int x, int y, int dst_w, int dst_h,
                    const uint32_t *src, int src_w, int src_h) {
    if (!fb_addr || !src || dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0) return;

    /* Clip destination rect to framebuffer bounds. */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + dst_w; if ((uint32_t)x1 > fb_width)  x1 = (int)fb_width;
    int y1 = y + dst_h; if ((uint32_t)y1 > fb_height) y1 = (int)fb_height;
    if (x0 >= x1 || y0 >= y1) return;

    int draw_w = x1 - x0;

    /* Precompute source-X indices for the output columns.
     * Static: kernel is single-threaded, so no re-entrancy issue.
     * Eliminates the division from the inner loop — the hot path is now just
     * two indexed loads and one store per pixel. */
    static uint16_t sx_map[2048];
    if (draw_w > 2048) return;
    for (int dx = 0; dx < draw_w; dx++)
        sx_map[dx] = (uint16_t)(((dx + x0 - x) * src_w) / dst_w);

    for (int py = y0; py < y1; py++) {
        int sy = ((py - y) * src_h) / dst_h;
        const uint32_t *src_row = src + (uint32_t)sy * (uint32_t)src_w;
        uint32_t       *dst_row = fb_back + (uint32_t)py * fb_width + (uint32_t)x0;
        for (int dx = 0; dx < draw_w; dx++)
            dst_row[dx] = src_row[sx_map[dx]];
    }
}

/* Read a pixel from the back buffer */
uint32_t fb_read_pixel(int x, int y) {
    if (!fb_addr || x < 0 || (uint32_t)x >= fb_width || y < 0 || (uint32_t)y >= fb_height)
        return 0;
    return fb_back[(uint32_t)y * fb_width + (uint32_t)x];
}

/* Alpha-blend color over the back buffer in the given rectangle.
 * Uses right-shift approximation: (a*alpha + b*(256-alpha)) >> 8 */
void fb_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    if (!fb_addr || alpha == 0 || w <= 0 || h <= 0) return;
    if (alpha == 255) { fb_fill_rect(x, y, w, h, color); return; }
    uint32_t sr = (color >> 16) & 0xFF;
    uint32_t sg = (color >>  8) & 0xFF;
    uint32_t sb =  color        & 0xFF;
    uint32_t ia = 256u - alpha;   /* inverse alpha (0..256) */
    for (int ry = y; ry < y + h; ry++) {
        if (ry < 0 || (uint32_t)ry >= fb_height) continue;
        uint32_t *row = fb_back + (uint32_t)ry * fb_width;
        for (int rx = x; rx < x + w; rx++) {
            if (rx < 0 || (uint32_t)rx >= fb_width) continue;
            uint32_t dst = row[rx];
            uint32_t r = (sr * alpha + ((dst >> 16) & 0xFF) * ia) >> 8;
            uint32_t g = (sg * alpha + ((dst >>  8) & 0xFF) * ia) >> 8;
            uint32_t b = (sb * alpha + ( dst        & 0xFF) * ia) >> 8;
            row[rx] = (r << 16) | (g << 8) | b;
        }
    }
}

/* Vertical gradient fill: linearly interpolate top→bottom colour */
void fb_fill_gradient_v(int x, int y, int w, int h,
                        uint32_t col_top, uint32_t col_bot) {
    if (!fb_addr || w <= 0 || h <= 0) return;
    uint32_t tr = (col_top >> 16) & 0xFF, tg = (col_top >> 8) & 0xFF, tb = col_top & 0xFF;
    uint32_t br = (col_bot >> 16) & 0xFF, bg = (col_bot >> 8) & 0xFF, bb = col_bot & 0xFF;
    for (int ry = y; ry < y + h; ry++) {
        if (ry < 0 || (uint32_t)ry >= fb_height) continue;
        int t  = ry - y;
        int dv = h > 1 ? h - 1 : 1;
        uint32_t r  = (tr * (dv - t) + br * t) / (uint32_t)dv;
        uint32_t g  = (tg * (dv - t) + bg * t) / (uint32_t)dv;
        uint32_t b  = (tb * (dv - t) + bb * t) / (uint32_t)dv;
        uint32_t col = (r << 16) | (g << 8) | b;
        uint32_t *row = fb_back + (uint32_t)ry * fb_width;
        for (int rx = x; rx < x + w; rx++) {
            if (rx < 0 || (uint32_t)rx >= fb_width) continue;
            row[rx] = col;
        }
    }
}

/* Screen-edge vignette: darken pixels proportionally to distance from centre.
 * Uses precomputed per-row/column lookup tables to avoid per-pixel division. */
void fb_vignette(uint32_t sw, uint32_t sh, uint8_t strength) {
    if (!fb_addr || sw == 0 || sh == 0 || strength == 0) return;
    uint32_t cx = sw / 2, cy = sh / 2;
    uint32_t max_r2 = cx * cx + cy * cy;
    if (max_r2 == 0) return;

    /* Per-axis squared-distance contributions (divisions done once per axis) */
    static uint8_t xtab[2048], ytab[2048];
    uint32_t sx = sw < 2048u ? sw : 2048u;
    uint32_t sy = sh < 2048u ? sh : 2048u;
    for (uint32_t x = 0; x < sx; x++) {
        int dx = (int)x - (int)cx;
        uint32_t v = (uint32_t)strength * (uint32_t)(dx * dx) / max_r2;
        xtab[x] = (uint8_t)(v > 255u ? 255u : v);
    }
    for (uint32_t y = 0; y < sy; y++) {
        int dy = (int)y - (int)cy;
        uint32_t v = (uint32_t)strength * (uint32_t)(dy * dy) / max_r2;
        ytab[y] = (uint8_t)(v > 255u ? 255u : v);
    }

    for (uint32_t y = 0; y < sh; y++) {
        uint32_t yd   = ytab[y < 2048u ? y : 2047u];
        uint32_t *row = fb_back + y * fb_width;
        for (uint32_t x = 0; x < sw; x++) {
            uint32_t dark = (uint32_t)xtab[x < 2048u ? x : 2047u] + yd;
            if (dark == 0) continue;
            if (dark > 255u) dark = 255u;
            uint32_t dst = row[x];
            uint32_t rr = (dst >> 16) & 0xFF;
            uint32_t gg = (dst >>  8) & 0xFF;
            uint32_t bb =  dst        & 0xFF;
            rr = rr > dark ? rr - dark : 0u;
            gg = gg > dark ? gg - dark : 0u;
            bb = bb > dark ? bb - dark : 0u;
            row[x] = (rr << 16) | (gg << 8) | bb;
        }
    }
}

/* Copy the back buffer to the real framebuffer in one pass.
 * Call once at the end of each fully-composed frame to eliminate flicker. */
void fb_flush(void) {
    if (!fb_addr) return;
    uint32_t dst_stride = fb_pitch / 4;
    uint32_t row_bytes  = fb_width * 4;
    for (uint32_t row = 0; row < fb_height; row++) {
        memcpy(fb_addr + row * dst_stride,
               fb_back + row * fb_width,
               row_bytes);
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

/* Draw a string rendering only the foreground glyph pixels.
 * Background pixels in each 8×8 cell are not touched, so the underlying
 * framebuffer content (glass tint, etc.) shows through. */
void fb_draw_string_px_fg(int x, int y, const char *s, uint32_t fg) {
    if (!fb_addr) return;
    int cx = x;
    while (*s) {
        char *bitmap = font8x8_basic[(uint8_t)(uint8_t)*s];
        for (int r = 0; r < 8; r++) {
            int py = y + r;
            if (py < 0 || (uint32_t)py >= fb_height) { cx += 8; s++; goto next; }
            for (int cb = 0; cb < 8; cb++) {
                int px2 = cx + cb;
                if (px2 < 0 || (uint32_t)px2 >= fb_width) continue;
                if (bitmap[r] & (1 << cb))
                    fb_back[(uint32_t)py * fb_width + (uint32_t)px2] = fg;
            }
        }
        cx += 8;
        s++;
        continue;
next:
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

/* Effective scroll region bounds clamped to the current grid. */
static uint32_t eff_scroll_top(void) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    return (scroll_top < max_rows) ? scroll_top : 0;
}

static uint32_t eff_scroll_bot(void) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    return (scroll_bot < max_rows) ? scroll_bot : max_rows - 1;
}

/*
 * Scroll the text rows [top..bot] (inclusive, 0-based) by n rows.
 *   n > 0 : content moves UP n rows (lines vanish at top, blanks at bottom)
 *   n < 0 : content moves DOWN |n| rows (blanks appear at top)
 * Updates the cell buffer and blits pixels (or VGA text memory).
 * Used by LF-at-margin scrolling, DECSTBM regions, and CSI L / CSI M.
 */
static void region_scroll(uint32_t top, uint32_t bot, int n) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;
    if (n == 0) return;
    if (bot >= max_rows) bot = max_rows - 1;
    if (top > bot) return;

    uint32_t span = bot - top + 1;
    uint32_t un   = (uint32_t)(n < 0 ? -n : n);
    if (un > span) un = span;

    /* ---- cell buffer ---- */
    uint32_t ccols = (max_cols < TERM_CELL_COLS) ? max_cols : TERM_CELL_COLS;
    if (n > 0) {
        for (uint32_t r = top; r + un <= bot && r + un < TERM_CELL_ROWS; r++) {
            for (uint32_t c = 0; c < ccols; c++) {
                cell_chars [r][c] = cell_chars [r + un][c];
                cell_colors[r][c] = cell_colors[r + un][c];
            }
        }
        for (uint32_t r = (bot >= un ? bot - un + 1 : top); r <= bot && r < TERM_CELL_ROWS; r++) {
            for (uint32_t c = 0; c < ccols; c++) {
                cell_chars [r][c] = ' ';
                cell_colors[r][c] = vga_color;
            }
        }
    } else {
        for (uint32_t r = bot; r >= top + un && r < TERM_CELL_ROWS; r--) {
            for (uint32_t c = 0; c < ccols; c++) {
                cell_chars [r][c] = cell_chars [r - un][c];
                cell_colors[r][c] = cell_colors[r - un][c];
            }
            if (r == 0) break;
        }
        for (uint32_t r = top; r < top + un && r <= bot && r < TERM_CELL_ROWS; r++) {
            for (uint32_t c = 0; c < ccols; c++) {
                cell_chars [r][c] = ' ';
                cell_colors[r][c] = vga_color;
            }
        }
    }

    /* ---- pixels ---- */
    if (fb_addr) {
        uint32_t stride = fb_pitch / 4;
        uint32_t ox = (uint32_t)(draw_off_x < 0 ? 0 : draw_off_x);
        uint32_t oy = (uint32_t)(draw_off_y < 0 ? 0 : draw_off_y);
        uint32_t cw = max_cols * 8;
        uint32_t py_top = oy + top * 8;
        uint32_t py_end = oy + (bot + 1) * 8;       /* exclusive */
        uint32_t shift  = un * 8;
        uint32_t bg     = vga_palette[(vga_color >> 4) & 0x0F];

        if (n > 0) {
            for (uint32_t py = py_top + shift; py < py_end; py++) {
                for (uint32_t px = ox; px < ox + cw; px++) {
                    uint32_t p = fb_back[py * fb_width + px];
                    fb_back[(py - shift) * fb_width + px] = p;
                    fb_addr[(py - shift) * stride   + px] = p;
                }
            }
            for (uint32_t py = py_end - shift; py < py_end; py++) {
                for (uint32_t px = ox; px < ox + cw; px++) {
                    fb_back[py * fb_width + px] = bg;
                    fb_addr[py * stride   + px] = bg;
                }
            }
        } else {
            for (uint32_t py = py_end - shift; py-- > py_top; ) {
                for (uint32_t px = ox; px < ox + cw; px++) {
                    uint32_t p = fb_back[py * fb_width + px];
                    fb_back[(py + shift) * fb_width + px] = p;
                    fb_addr[(py + shift) * stride   + px] = p;
                }
            }
            for (uint32_t py = py_top; py < py_top + shift && py < py_end; py++) {
                for (uint32_t px = ox; px < ox + cw; px++) {
                    fb_back[py * fb_width + px] = bg;
                    fb_addr[py * stride   + px] = bg;
                }
            }
        }
    } else {
        if (n > 0) {
            for (uint32_t y = top; y + un <= bot; y++)
                for (uint32_t x = 0; x < VGA_WIDTH; x++)
                    VGA_MEMORY[y * VGA_WIDTH + x] =
                        VGA_MEMORY[(y + un) * VGA_WIDTH + x];
            for (uint32_t y = bot - un + 1; y <= bot; y++)
                for (uint32_t x = 0; x < VGA_WIDTH; x++)
                    VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        } else {
            for (uint32_t y = bot; y >= top + un; y--) {
                for (uint32_t x = 0; x < VGA_WIDTH; x++)
                    VGA_MEMORY[y * VGA_WIDTH + x] =
                        VGA_MEMORY[(y - un) * VGA_WIDTH + x];
                if (y == 0) break;
            }
            for (uint32_t y = top; y < top + un && y <= bot; y++)
                for (uint32_t x = 0; x < VGA_WIDTH; x++)
                    VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
        }
    }
}

/*
 * Line feed with VT100 margin semantics: scroll only when the cursor
 * sits on the bottom margin of the scroll region; below the region it
 * just moves down and stops at the screen edge.  With the region unset
 * this is exactly the old "scroll whole screen at the bottom" behaviour.
 */
static void vga_lf(void) {
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    uint32_t bot = eff_scroll_bot();
    if (cursor_row == bot) {
        region_scroll(eff_scroll_top(), bot, 1);
    } else if (cursor_row + 1 < max_rows) {
        cursor_row++;
    }
}

/* ------------------------------------------------------------------ */
/* Software cursor (framebuffer mode only)                             */
/* ------------------------------------------------------------------ */

static void cursor_undraw(void) {
    if (!cursor_drawn) return;
    cursor_drawn = 0;
    if (!fb_addr) return;
    if (cdrawn_row >= TERM_CELL_ROWS || cdrawn_col >= TERM_CELL_COLS) return;
    uint8_t col = cell_colors[cdrawn_row][cdrawn_col];
    draw_char_rgb(cell_chars[cdrawn_row][cdrawn_col],
                  (int)(draw_off_x + (int32_t)(cdrawn_col * 8)),
                  (int)(draw_off_y + (int32_t)(cdrawn_row * 8)),
                  vga_palette[col & 0x0F],
                  vga_palette[(col >> 4) & 0x0F]);
}

static void cursor_draw(void) {
    if (!fb_addr || !cursor_visible) return;
    if (cursor_row >= TERM_CELL_ROWS || cursor_col >= TERM_CELL_COLS) return;
    uint8_t col = cell_colors[cursor_row][cursor_col];
    /* Inverse video: swap fg/bg of the underlying cell. */
    draw_char_rgb(cell_chars[cursor_row][cursor_col],
                  (int)(draw_off_x + (int32_t)(cursor_col * 8)),
                  (int)(draw_off_y + (int32_t)(cursor_row * 8)),
                  vga_palette[(col >> 4) & 0x0F],
                  vga_palette[col & 0x0F]);
    cursor_drawn = 1;
    cdrawn_row = cursor_row;
    cdrawn_col = cursor_col;
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
    cursor_drawn = 0;   /* every cell is repainted below */
    for (uint32_t y = 0; y < max_rows; y++) {
        for (uint32_t x = 0; x < max_cols; x++) {
            draw_char(' ', x, y, vga_color);
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    vga_update_cursor();
    cursor_draw();
}

/* ------------------------------------------------------------------ */
/* ANSI helpers                                                         */
/* ------------------------------------------------------------------ */

static void ansi_erase_line(int mode) {
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;
    uint32_t start = 0, end = max_cols;
    if (mode == 0) start = cursor_col;          /* to end */
    else if (mode == 1) end = cursor_col + 1;   /* to beginning */
    for (uint32_t c = start; c < end; c++)
        draw_char(' ', c, cursor_row, vga_color);
}

static void ansi_erase_display(int mode) {
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    if (mode == 2 || mode == 3) {
        for (uint32_t r = 0; r < max_rows; r++)
            for (uint32_t c = 0; c < max_cols; c++)
                draw_char(' ', c, r, vga_color);
        cursor_row = 0; cursor_col = 0;
    } else if (mode == 0) {
        /* from cursor to end */
        ansi_erase_line(0);
        uint32_t max_r = fb_addr ? fb_rows : VGA_HEIGHT;
        for (uint32_t r = cursor_row + 1; r < max_r; r++)
            for (uint32_t c = 0; c < max_cols; c++)
                draw_char(' ', c, r, vga_color);
    } else if (mode == 1) {
        /* from beginning to cursor */
        for (uint32_t r = 0; r < cursor_row; r++)
            for (uint32_t c = 0; c < max_cols; c++)
                draw_char(' ', c, r, vga_color);
        ansi_erase_line(1);
    }
}

static void ansi_set_color(int p) {
    if (p == 0) { vga_color = 0x0F; return; }  /* reset */
    if (p == 1) { vga_color |= 0x08; return; }  /* bold → high intensity fg */
    if (p == 7) {                                /* reverse */
        uint8_t fg = vga_color & 0x0F;
        uint8_t bg = (vga_color >> 4) & 0x0F;
        vga_color = (uint8_t)(fg << 4) | bg;
        return;
    }
    if (p >= 30 && p <= 37) {
        vga_color = (vga_color & 0xF0) | ansi_to_vga[p - 30];
    } else if (p >= 90 && p <= 97) {
        vga_color = (vga_color & 0xF0) | (ansi_to_vga[p - 90] | 0x08);
    } else if (p >= 40 && p <= 47) {
        vga_color = (vga_color & 0x0F) | (uint8_t)(ansi_to_vga[p - 40] << 4);
    } else if (p >= 100 && p <= 107) {
        vga_color = (vga_color & 0x0F) | (uint8_t)((ansi_to_vga[p - 100] | 0x08) << 4);
    }
}

static void ansi_dispatch(char cmd) {
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;
    uint32_t max_rows = fb_addr ? fb_rows : VGA_HEIGHT;
    int p0 = ansi_nparams > 0 ? ansi_params[0] : 0;
    int p1 = ansi_nparams > 1 ? ansi_params[1] : 0;

    switch (cmd) {
    case 'A':   /* cursor up */
        if (p0 < 1) p0 = 1;
        cursor_row = (cursor_row >= (uint32_t)p0) ? cursor_row - (uint32_t)p0 : 0;
        break;
    case 'B':   /* cursor down */
        if (p0 < 1) p0 = 1;
        cursor_row += (uint32_t)p0;
        if (cursor_row >= max_rows) cursor_row = max_rows - 1;
        break;
    case 'C':   /* cursor forward */
        if (p0 < 1) p0 = 1;
        cursor_col += (uint32_t)p0;
        if (cursor_col >= max_cols) cursor_col = max_cols - 1;
        break;
    case 'D':   /* cursor back */
        if (p0 < 1) p0 = 1;
        cursor_col = (cursor_col >= (uint32_t)p0) ? cursor_col - (uint32_t)p0 : 0;
        break;
    case 'H':   /* cursor position (1-based) */
    case 'f':
        cursor_row = (p0 > 0 ? (uint32_t)(p0 - 1) : 0);
        cursor_col = (p1 > 0 ? (uint32_t)(p1 - 1) : 0);
        if (cursor_row >= max_rows) cursor_row = max_rows - 1;
        if (cursor_col >= max_cols) cursor_col = max_cols - 1;
        break;
    case 'G':   /* CHA — cursor horizontal absolute (1-based) */
        cursor_col = (p0 > 0) ? (uint32_t)(p0 - 1) : 0;
        if (cursor_col >= max_cols) cursor_col = max_cols - 1;
        break;
    case 'd':   /* VPA — vertical position absolute (1-based) */
        cursor_row = (p0 > 0) ? (uint32_t)(p0 - 1) : 0;
        if (cursor_row >= max_rows) cursor_row = max_rows - 1;
        break;
    case 'J':   /* erase display */
        ansi_erase_display(p0);
        break;
    case 'K':   /* erase line */
        ansi_erase_line(p0);
        break;
    case 'L': { /* IL — insert blank lines at cursor (within region) */
        uint32_t bot = eff_scroll_bot();
        if (cursor_row >= eff_scroll_top() && cursor_row <= bot)
            region_scroll(cursor_row, bot, -(p0 > 0 ? p0 : 1));
        break;
    }
    case 'M': { /* DL — delete lines at cursor (within region) */
        uint32_t bot = eff_scroll_bot();
        if (cursor_row >= eff_scroll_top() && cursor_row <= bot)
            region_scroll(cursor_row, bot, (p0 > 0 ? p0 : 1));
        break;
    }
    case 'r':   /* DECSTBM — set scroll region (1-based, inclusive) */
        if (p0 <= 0 && p1 <= 0) {
            scroll_top = 0;
            scroll_bot = 0xFFFFFFFFu;
        } else if (p0 >= 1 && p1 > p0) {
            scroll_top = (uint32_t)(p0 - 1);
            scroll_bot = (uint32_t)(p1 - 1);
        }
        /* VT100: cursor moves to home after DECSTBM */
        cursor_row = 0;
        cursor_col = 0;
        break;
    case 'm':   /* SGR — set graphics rendition */
        if (ansi_nparams == 0) {
            ansi_set_color(0);
        } else {
            for (int i = 0; i < ansi_nparams; i++)
                ansi_set_color(ansi_params[i]);
        }
        break;
    case 's':   /* save cursor */
        saved_cur_row = cursor_row;
        saved_cur_col = cursor_col;
        break;
    case 'u':   /* restore cursor */
        cursor_row = (saved_cur_row < max_rows) ? saved_cur_row : max_rows - 1;
        cursor_col = (saved_cur_col < max_cols) ? saved_cur_col : max_cols - 1;
        break;
    case 'l':   /* private mode reset (?25l = hide cursor) */
        if (ansi_priv && p0 == 25) cursor_visible = 0;
        break;
    case 'h':   /* private mode set (?25h = show cursor) */
        if (ansi_priv && p0 == 25) cursor_visible = 1;
        break;
    default:
        break;
    }
}

static void vga_putc_inner(char c) {
    uint32_t max_cols = fb_addr ? fb_cols : VGA_WIDTH;

    /* ANSI / VT100 state machine */
    if (ansi_state == ANSI_ESC) {
        if (c == '[') {
            ansi_state   = ANSI_CSI;
            ansi_nparams = 0;
            ansi_priv    = 0;
            ansi_params[0] = 0;
        } else {
            ansi_state = ANSI_NORMAL;
        }
        return;
    }

    if (ansi_state == ANSI_CSI) {
        if (c == '?') {
            ansi_priv = 1;
        } else if (c >= '0' && c <= '9') {
            if (ansi_nparams == 0) ansi_nparams = 1;
            ansi_params[ansi_nparams - 1] =
                ansi_params[ansi_nparams - 1] * 10 + (c - '0');
        } else if (c == ';') {
            ansi_nparams++;
            if (ansi_nparams < ANSI_MAX_PARAMS)
                ansi_params[ansi_nparams - 1] = 0;
            else
                ansi_nparams = ANSI_MAX_PARAMS;
        } else {
            /* Final byte */
            ansi_dispatch(c);
            ansi_state = ANSI_NORMAL;
        }
        vga_update_cursor();
        return;
    }

    /* ANSI_NORMAL */
    if (c == '\033') {
        ansi_state = ANSI_ESC;
        return;
    }

    if (c == '\n') {
        cursor_col = 0;
        vga_lf();
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
    } else if (c == '\t') {
        uint32_t next = (cursor_col + 8) & ~7U;
        while (cursor_col < next && cursor_col < max_cols)
            draw_char(' ', cursor_col++, cursor_row, vga_color);
    } else {
        draw_char(c, cursor_col, cursor_row, vga_color);
        cursor_col++;
        if (cursor_col >= max_cols) {
            cursor_col = 0;
            vga_lf();
        }
    }

    vga_update_cursor();
}

void vga_putc(char c) {
    cursor_undraw();
    vga_putc_inner(c);
    cursor_draw();
}

void vga_text_dims(uint32_t *cols, uint32_t *rows) {
    uint32_t c = fb_addr ? fb_cols : VGA_WIDTH;
    uint32_t r = fb_addr ? fb_rows : VGA_HEIGHT;
    if (c > TERM_CELL_COLS) c = TERM_CELL_COLS;
    if (r > TERM_CELL_ROWS) r = TERM_CELL_ROWS;
    *cols = c;
    *rows = r;
}

void vga_term_reset(void) {
    cursor_undraw();
    scroll_top     = 0;
    scroll_bot     = 0xFFFFFFFFu;
    saved_cur_row  = 0;
    saved_cur_col  = 0;
    cursor_visible = 1;
    ansi_state     = ANSI_NORMAL;
    vga_color      = 0x0F;
    cursor_draw();
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
    cursor_undraw();
    cursor_row = pos / max_cols;
    cursor_col = pos % max_cols;
    vga_update_cursor();
    cursor_draw();
}
