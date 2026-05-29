#include "kapp.h"
#include "string.h"

/* ================================================================
 * Paint — click/drag to draw on a pixel canvas
 * Static canvas lives in BSS (never kmalloc'd)
 * ================================================================ */

#define CV_W  300
#define CV_H  200
#define CV_X  4
#define CV_Y  4

static uint32_t canvas[CV_W * CV_H];
static int      cur_color = 15;   /* index into palette */
static int      brush     = 2;

static const uint32_t pal[16] = {
    0x000000U, 0x0000BBU, 0x00BB00U, 0x00BBBBU,
    0xBB0000U, 0xBB00BBU, 0xBBAA00U, 0xBBBBBBU,
    0x555555U, 0x5555FFU, 0x55FF55U, 0x55FFFFU,
    0xFF5555U, 0xFF55FFU, 0xFFFF55U, 0xFFFFFFU,
};

static void cv_paint(int px, int py) {
    for (int dy = -brush; dy <= brush; dy++)
        for (int dx = -brush; dx <= brush; dx++) {
            int x = px + dx, y = py + dy;
            if (x >= 0 && x < CV_W && y >= 0 && y < CV_H)
                canvas[y * CV_W + x] = pal[cur_color];
        }
}

void paint_create(int wi) {
    (void)wi;
    for (int i = 0; i < CV_W * CV_H; i++) canvas[i] = 0xFFFFFFU; /* white */
    cur_color = 0; /* start with black pen */
    brush = 1;
}
void paint_destroy(int wi) { (void)wi; }
void paint_tick(int wi)    { (void)wi; }

void paint_key(int wi, int kt, char ch) {
    (void)wi; (void)kt;
    if (ch >= '0' && ch <= '9') cur_color = ch - '0';
    if (ch == '+' && brush < 8) brush++;
    if (ch == '-' && brush > 0) brush--;
    if (ch == 'c' || ch == 'C')
        for (int i = 0; i < CV_W * CV_H; i++) canvas[i] = 0xFFFFFFU;
}

void paint_click(int wi, int x, int y) {
    (void)wi;
    /* Palette row click (below canvas) */
    if (y >= CV_Y + CV_H + 6 && y < CV_Y + CV_H + 22) {
        int col = (x - CV_X) / 20;
        if (col >= 0 && col < 16) cur_color = col;
        return;
    }
    /* Canvas */
    if (x >= CV_X && x < CV_X + CV_W && y >= CV_Y && y < CV_Y + CV_H)
        cv_paint(x - CV_X, y - CV_Y);
}

void paint_mouse(int wi, int x, int y, int btn) {
    (void)wi;
    if (!btn) return;
    if (x >= CV_X && x < CV_X + CV_W && y >= CV_Y && y < CV_Y + CV_H)
        cv_paint(x - CV_X, y - CV_Y);
}

void paint_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi; (void)cw; (void)ch;

    kd_fill(cx, cy, cw, ch, 0x181818U);

    /* Canvas */
    fb_blit_pixels(cx + CV_X, cy + CV_Y, canvas, CV_W, CV_H);
    kd_rect(cx + CV_X - 1, cy + CV_Y - 1, CV_W + 2, CV_H + 2, KA_BORDER);

    /* Color palette — 16 swatches in a row */
    int py = cy + CV_Y + CV_H + 6;
    for (int i = 0; i < 16; i++) {
        int sx = cx + CV_X + i * 20;
        kd_fill(sx, py, 18, 14, pal[i]);
        if (i == cur_color)
            kd_rect(sx - 1, py - 1, 20, 16, 0xFFFFFFU);
    }

    /* Info */
    int iy = py + 18;
    kd_str(cx + CV_X, iy,     "0-9: color  +/-: brush  C: clear", KA_DIM, 0x181818U);

    /* Current brush size indicator */
    char buf[4];
    kd_str(cx + CV_X + 270, iy, "Br:", KA_DIM, 0x181818U);
    kd_itoa(brush, buf, sizeof(buf));
    kd_str(cx + CV_X + 294, iy, buf, KA_BRIGHT, 0x181818U);
}
