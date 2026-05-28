/*
 * paint.c — MacPaint-style pixel drawing app for SiMPLE OS
 *
 * Ring-3 user program using WM syscalls (SYS_WM_CREATE / BLIT / EVENT).
 *
 * Layout (client area, 400 × 300 px):
 *   y =   0 .. 31   : toolbar  (4 tool buttons + 3 brush-size buttons + colour swatch)
 *   y =  32 .. 275  : canvas   (white drawing area)
 *   y = 276 .. 299  : palette  (16 colour swatches)
 *
 * Tools:
 *   P — Pencil   : freehand draw with current colour
 *   E — Eraser   : freehand draw with white
 *   F — Fill     : flood-fill region with current colour
 *   C — Clear    : fill entire canvas with white (momentary button)
 *
 * Brush sizes: 1 px · 3 px · 5 px (square brush)
 *
 * Keyboard shortcuts:
 *   ESC    — exit
 *   P/E/F  — switch tool
 *
 * Press the [X] title-bar button or ESC to quit.
 */

#include "wm.h"

/* ── extern declarations (provided by libc.c) ── */
void exit(int code);
int  wm_create(int x, int y, int w, int h);
int  wm_destroy(int wid);
int  wm_blit(int wid, unsigned int *buf, int len);
int  wm_event(wm_event_t *ev, int max);
int  wm_setfocus(int wid);

/* ── window geometry ── */
#define WIN_X    60
#define WIN_Y    30
#define WIN_W   400
#define WIN_H   300

/* ── layout ── */
#define TOOLBAR_H  32
#define PALETTE_H  24
#define CANVAS_Y   TOOLBAR_H
#define CANVAS_H   (WIN_H - TOOLBAR_H - PALETTE_H)

/* ── colours ── */
#define C_TOOLBAR   0x2B2B2BU
#define C_WHITE     0xFFFFFFU
#define C_BLACK     0x000000U
#define C_SELEDGE   0xFFD700U   /* gold ring around selected item */

/* 16-entry colour palette (classic desktop palette) */
static const unsigned int PAL[16] = {
    0x000000U, 0xFFFFFFU, 0xFF0000U, 0x00CC00U,
    0x0000FFU, 0xFFFF00U, 0xFF8800U, 0xFF00FFU,
    0x00FFFFU, 0x884400U, 0x888800U, 0x008888U,
    0x880088U, 0xAA6633U, 0x44AA44U, 0xAAAAAAU,
};

/* ── pixel buffer (400 × 300 × 4 = 480 KB in BSS) ── */
static unsigned int pixels[WIN_W * WIN_H];

/* ── tools ── */
#define TOOL_PENCIL  0
#define TOOL_ERASER  1
#define TOOL_FILL    2
#define TOOL_CLEAR   3

/* ── app state ── */
static int tool    = TOOL_PENCIL;
static int col_idx = 0;           /* index into PAL[]  */
static int brush   = 1;           /* actual pixel size: 1, 3, or 5 */
static int drawing = 0;           /* 1 while left button is held   */
static int prev_x  = -1;
static int prev_y  = -1;

/* ── scanline flood-fill queue (2 KB) ── */
#define FQSZ 512
static short fq_x[FQSZ], fq_y[FQSZ];
static int   fq_head, fq_tail;

/* ──────────────────────────────────────────────────────────────────────────
   Pixel primitives
   ────────────────────────────────────────────────────────────────────────── */

static inline void pset(int x, int y, unsigned int c) {
    if ((unsigned)x < WIN_W && (unsigned)y < WIN_H)
        pixels[y * WIN_W + x] = c;
}
static inline unsigned int pget(int x, int y) {
    if ((unsigned)x < WIN_W && (unsigned)y < WIN_H)
        return pixels[y * WIN_W + x];
    return 0xDEADBEEFU;
}
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            pset(x + dx, y + dy, c);
}

/* Draw a square brush centred at (cx,cy), clipped to canvas region */
static void brush_dot(int cx, int cy, unsigned int c, int bsz) {
    int r = bsz / 2;
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < CANVAS_Y || py >= CANVAS_Y + CANVAS_H) continue;
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx;
            if ((unsigned)px < WIN_W)
                pixels[py * WIN_W + px] = c;
        }
    }
}

/* Bresenham line using brush_dot */
static void draw_line(int x0, int y0, int x1, int y1,
                      unsigned int c, int bsz) {
    int adx = x1 - x0; if (adx < 0) adx = -adx;
    int ady = y1 - y0; if (ady < 0) ady = -ady;
    int sx  = (x1 >= x0) ? 1 : -1;
    int sy  = (y1 >= y0) ? 1 : -1;
    int err = adx - ady;
    for (;;) {
        brush_dot(x0, y0, c, bsz);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -ady) { err -= ady; x0 += sx; }
        if (e2 <  adx) { err += adx; y0 += sy; }
    }
}

/* ──────────────────────────────────────────────────────────────────────────
   Scanline flood fill (proper span-seeding, O(H) queue depth)
   ────────────────────────────────────────────────────────────────────────── */

static void fq_push(int x, int y) {
    int nt = (fq_tail + 1) % FQSZ;
    if (nt != fq_head) {
        fq_x[fq_tail] = (short)x;
        fq_y[fq_tail] = (short)y;
        fq_tail = nt;
    }
}

static void flood_fill(int sx, int sy, unsigned int nc) {
    if (sy < CANVAS_Y || sy >= CANVAS_Y + CANVAS_H) return;
    if ((unsigned)sx >= WIN_W) return;
    unsigned int oc = pget(sx, sy);
    if (oc == nc) return;

    fq_head = fq_tail = 0;
    fq_push(sx, sy);

    while (fq_head != fq_tail) {
        int x = fq_x[fq_head], y = fq_y[fq_head];
        fq_head = (fq_head + 1) % FQSZ;

        if (pget(x, y) != oc) continue;  /* already filled by earlier span */

        /* Scan left */
        int lx = x;
        while (lx > 0 && pget(lx - 1, y) == oc) lx--;
        /* Scan right */
        int rx = x;
        while (rx < WIN_W - 1 && pget(rx + 1, y) == oc) rx++;

        /* Fill the horizontal span */
        for (int xi = lx; xi <= rx; xi++)
            pixels[y * WIN_W + xi] = nc;

        /* Seed row above — push one entry per contiguous run */
        int in_seg = 0;
        for (int xi = lx; xi <= rx; xi++) {
            if (y > CANVAS_Y && pget(xi, y - 1) == oc) {
                if (!in_seg) { fq_push(xi, y - 1); in_seg = 1; }
            } else {
                in_seg = 0;
            }
        }
        /* Seed row below */
        in_seg = 0;
        for (int xi = lx; xi <= rx; xi++) {
            if (y < CANVAS_Y + CANVAS_H - 1 && pget(xi, y + 1) == oc) {
                if (!in_seg) { fq_push(xi, y + 1); in_seg = 1; }
            } else {
                in_seg = 0;
            }
        }
    }
}

/* ──────────────────────────────────────────────────────────────────────────
   Minimal 8 × 8 bitmap font
   ────────────────────────────────────────────────────────────────────────── */

static const unsigned char FONT[128][8] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['P'] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    ['E'] = {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},
    ['F'] = {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},
    ['C'] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['3'] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['5'] = {0x7E,0x60,0x60,0x7C,0x06,0x06,0x7E,0x00},
};

static void draw_glyph(int x, int y, unsigned char ch, unsigned int fg) {
    if (ch >= 128) return;
    for (int row = 0; row < 8; row++) {
        unsigned char bits = FONT[ch][row];
        for (int col = 0; col < 8; col++)
            if (bits & (0x80u >> col))
                pset(x + col, y + row, fg);
    }
}

static void draw_cstr(int x, int y, const char *s, unsigned int fg) {
    for (; *s; s++, x += 9)
        draw_glyph(x, y, (unsigned char)*s, fg);
}

/* ──────────────────────────────────────────────────────────────────────────
   Toolbar geometry
   ────────────────────────────────────────────────────────────────────────── */

#define TBTN_W   36
#define TBTN_H   24
#define TBTN_PAD  4

#define BBTN_W   22
#define BBTN_H   20
#define BBTN_PAD  4

static int tbtn_x(int i) { return TBTN_PAD + i * (TBTN_W + TBTN_PAD); }
static int tbtn_y_top(void) { return (TOOLBAR_H - TBTN_H) / 2; }

static int bbtn_x(int i) {
    /* start right of 4 tool buttons + 12 px separator */
    return 4 * (TBTN_W + TBTN_PAD) + 12 + i * (BBTN_W + BBTN_PAD);
}
static int bbtn_y_top(void) { return (TOOLBAR_H - BBTN_H) / 2; }

static const char *TOOL_LABELS[4] = {"P", "E", "F", "C"};
static const char *BRUSH_LABELS[3] = {"1", "3", "5"};
static const int   BRUSH_SIZES[3]  = {1, 3, 5};

/* Draw a recessed button with optional gold selection border */
static void draw_button(int bx, int by, int bw, int bh,
                        int selected, const char *label) {
    unsigned int bg = selected ? 0x555555U : 0x404040U;
    fill_rect(bx, by, bw, bh, bg);
    /* 3D bevel */
    fill_rect(bx,        by,        bw, 1, selected ? 0x777777U : 0x666666U);
    fill_rect(bx,        by,        1, bh, selected ? 0x777777U : 0x666666U);
    fill_rect(bx,        by + bh-1, bw, 1, 0x1A1A1AU);
    fill_rect(bx + bw-1, by,        1, bh, 0x1A1A1AU);
    /* Gold ring on selected */
    if (selected) {
        fill_rect(bx,        by,        bw, 1, C_SELEDGE);
        fill_rect(bx,        by + bh-1, bw, 1, C_SELEDGE);
        fill_rect(bx,        by,        1, bh, C_SELEDGE);
        fill_rect(bx + bw-1, by,        1, bh, C_SELEDGE);
    }
    /* Centred label */
    int tx = bx + (bw - 8) / 2;
    int ty = by + (bh - 8) / 2;
    unsigned int fg = selected ? 0xFFFFFFU : 0xAAAAAAU;
    draw_cstr(tx, ty, label, fg);
}

/* ──────────────────────────────────────────────────────────────────────────
   Rendering
   ────────────────────────────────────────────────────────────────────────── */

static void draw_toolbar(void) {
    fill_rect(0, 0, WIN_W, TOOLBAR_H, C_TOOLBAR);

    /* Tool buttons */
    int ty = tbtn_y_top();
    for (int i = 0; i < 4; i++)
        draw_button(tbtn_x(i), ty, TBTN_W, TBTN_H,
                    (i == tool), TOOL_LABELS[i]);

    /* Brush-size buttons */
    int by = bbtn_y_top();
    for (int i = 0; i < 3; i++)
        draw_button(bbtn_x(i), by, BBTN_W, BBTN_H,
                    (BRUSH_SIZES[i] == brush), BRUSH_LABELS[i]);

    /* Current-colour swatch (right edge of toolbar) */
    int swx = WIN_W - 38, swy = 4;
    fill_rect(swx - 2, swy - 2, 30, 30, 0x000000U); /* black border */
    fill_rect(swx, swy, 26, 26, PAL[col_idx]);
}

static void draw_palette(void) {
    int py  = WIN_H - PALETTE_H;
    int sw  = WIN_W / 16;           /* 25 px per swatch */
    fill_rect(0, py, WIN_W, PALETTE_H, 0x1A1A1AU);
    for (int i = 0; i < 16; i++) {
        int sx = i * sw;
        fill_rect(sx, py + 2, sw - 1, PALETTE_H - 4, PAL[i]);
        if (i == col_idx) {
            fill_rect(sx,          py + 2,           sw - 1, 1, C_SELEDGE);
            fill_rect(sx,          py + PALETTE_H-3, sw - 1, 1, C_SELEDGE);
            fill_rect(sx,          py + 2,           1, PALETTE_H-4, C_SELEDGE);
            fill_rect(sx + sw - 2, py + 2,           1, PALETTE_H-4, C_SELEDGE);
        }
    }
}

/* Thin separator lines at canvas boundaries */
static void draw_separators(void) {
    fill_rect(0, CANVAS_Y - 1,             WIN_W, 1, 0x111111U);
    fill_rect(0, CANVAS_Y + CANVAS_H,      WIN_W, 1, 0x111111U);
}

static void clear_canvas(void) {
    fill_rect(0, CANVAS_Y, WIN_W, CANVAS_H, C_WHITE);
}

static void redraw_ui(void) {
    draw_toolbar();
    draw_separators();
    draw_palette();
}

/* ──────────────────────────────────────────────────────────────────────────
   Hit testing
   ────────────────────────────────────────────────────────────────────────── */

static int hit_tool(int mx, int my) {
    int by = tbtn_y_top();
    if (my < by || my >= by + TBTN_H) return -1;
    for (int i = 0; i < 4; i++) {
        int bx = tbtn_x(i);
        if (mx >= bx && mx < bx + TBTN_W) return i;
    }
    return -1;
}

static int hit_brush(int mx, int my) {
    int by = bbtn_y_top();
    if (my < by || my >= by + BBTN_H) return -1;
    for (int i = 0; i < 3; i++) {
        int bx = bbtn_x(i);
        if (mx >= bx && mx < bx + BBTN_W) return i;
    }
    return -1;
}

static int hit_palette(int mx, int my) {
    int py = WIN_H - PALETTE_H;
    if (my < py || my >= WIN_H) return -1;
    int sw = WIN_W / 16;
    int i  = mx / sw;
    if (i < 0)  i = 0;
    if (i > 15) i = 15;
    return i;
}

static int in_canvas(int mx, int my) {
    return ((unsigned)mx < WIN_W &&
            my >= CANVAS_Y &&
            my <  CANVAS_Y + CANVAS_H);
}

/* ──────────────────────────────────────────────────────────────────────────
   Paint stroke
   ────────────────────────────────────────────────────────────────────────── */

static void do_paint(int x, int y) {
    if (!in_canvas(x, y)) return;
    unsigned int c = (tool == TOOL_ERASER) ? C_WHITE : PAL[col_idx];
    if (prev_x >= 0 && prev_y >= 0)
        draw_line(prev_x, prev_y, x, y, c, brush);
    else
        brush_dot(x, y, c, brush);
    prev_x = x;
    prev_y = y;
}

/* ──────────────────────────────────────────────────────────────────────────
   Entry point
   ────────────────────────────────────────────────────────────────────────── */

/* PS/2 scancodes for keyboard shortcuts */
#define SC_P   0x19
#define SC_E   0x12
#define SC_F   0x21

void _start(void) {
    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) { exit(1); }

    wm_setfocus(wid);
    clear_canvas();
    redraw_ui();
    wm_blit(wid, pixels, WIN_W * WIN_H * 4);

    wm_event_t ev;
    for (;;) {
        int r = wm_event(&ev, (int)sizeof(ev));
        if (r <= 0) continue;

        /* ── Exit conditions ── */
        if (r == WM_EV_CLOSE) break;
        if (r == WM_EV_KEY_DOWN && (ev.x & 0xFF) == SC_ESC) break;

        /* ── Keyboard shortcuts ── */
        if (r == WM_EV_KEY_DOWN) {
            int sc = ev.x & 0xFF;
            int changed = 1;
            if      (sc == SC_P) tool = TOOL_PENCIL;
            else if (sc == SC_E) tool = TOOL_ERASER;
            else if (sc == SC_F) tool = TOOL_FILL;
            else changed = 0;
            if (changed) {
                redraw_ui();
                wm_blit(wid, pixels, WIN_W * WIN_H * 4);
            }
            continue;
        }

        int mx = (int)ev.x, my = (int)ev.y;
        int need_blit = 0;

        /* ── Left-button press ── */
        if (r == WM_EV_MOUSE_BTN && (ev.btn & 1)) {

            /* Toolbar */
            if (my < TOOLBAR_H) {
                int ti = hit_tool(mx, my);
                if (ti >= 0) {
                    if (ti == TOOL_CLEAR) {
                        clear_canvas();
                        need_blit = 1;
                    } else {
                        tool = ti;
                    }
                    redraw_ui();
                    need_blit = 1;
                } else {
                    int bi = hit_brush(mx, my);
                    if (bi >= 0) {
                        brush = BRUSH_SIZES[bi];
                        redraw_ui();
                        need_blit = 1;
                    }
                }
            }
            /* Palette */
            else if (my >= WIN_H - PALETTE_H) {
                int ci = hit_palette(mx, my);
                if (ci >= 0) {
                    col_idx = ci;
                    redraw_ui();
                    need_blit = 1;
                }
            }
            /* Canvas */
            else if (in_canvas(mx, my)) {
                if (tool == TOOL_FILL) {
                    flood_fill(mx, my, PAL[col_idx]);
                    need_blit = 1;
                } else {
                    drawing = 1;
                    prev_x = -1;
                    prev_y = -1;
                    do_paint(mx, my);
                    need_blit = 1;
                }
            }
        }

        /* ── Left-button release ── */
        if (r == WM_EV_MOUSE_BTN && !(ev.btn & 1)) {
            drawing = 0;
            prev_x  = -1;
            prev_y  = -1;
        }

        /* ── Mouse move while drawing ── */
        if (r == WM_EV_MOUSE_MOV) {
            if (ev.btn & 1) {
                if (drawing && in_canvas(mx, my)) {
                    do_paint(mx, my);
                    need_blit = 1;
                }
            } else {
                /* Button lifted outside a BTN event — sync state */
                drawing = 0;
                prev_x  = -1;
                prev_y  = -1;
            }
        }

        if (need_blit)
            wm_blit(wid, pixels, WIN_W * WIN_H * 4);
    }

    wm_destroy(wid);
    exit(0);
}
