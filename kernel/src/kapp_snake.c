#include "kapp.h"
#include "pit.h"
#include "string.h"

/* Snake — 28x18 cell grid, 10px cells, arrow-key controlled */
#define SN_COLS  28
#define SN_ROWS  18
#define SN_CELL  10
#define SN_MAX   (SN_COLS * SN_ROWS)
#define SN_SPEED 8   /* ticks between moves (~80 ms at 100 Hz) */

typedef struct { int8_t x, y; } sn_pt_t;

static sn_pt_t sn_body[SN_MAX];
static int     sn_len;
static int     sn_dx, sn_dy, sn_ndx, sn_ndy;
static sn_pt_t sn_food;
static int     sn_score, sn_best, sn_over, sn_started;
static uint32_t sn_seed, sn_last;

static void sn_rand_food(void) {
    for (int t = 0; t < 300; t++) {
        sn_seed = sn_seed * 1664525u + 1013904223u;
        int fx = (int)((sn_seed >> 16) % (uint32_t)SN_COLS);
        sn_seed = sn_seed * 1664525u + 1013904223u;
        int fy = (int)((sn_seed >>  8) % (uint32_t)SN_ROWS);
        int ok = 1;
        for (int i = 0; i < sn_len; i++)
            if (sn_body[i].x == (int8_t)fx && sn_body[i].y == (int8_t)fy)
                { ok = 0; break; }
        if (ok) { sn_food.x = (int8_t)fx; sn_food.y = (int8_t)fy; return; }
    }
}

static void sn_restart(void) {
    sn_len = 4;
    for (int i = 0; i < sn_len; i++) {
        sn_body[i].x = (int8_t)(10 - i);
        sn_body[i].y = 9;
    }
    sn_dx = sn_ndx = 1; sn_dy = sn_ndy = 0;
    sn_score = 0; sn_over = 0; sn_started = 0;
    sn_seed ^= (uint32_t)pit_ticks();
    sn_rand_food();
    sn_last = pit_ticks();
}

void snake_create(int wi)  { (void)wi; sn_best = 0; sn_seed = 54321u; sn_restart(); }
void snake_destroy(int wi) { (void)wi; }
void snake_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void snake_click(int wi, int x, int y) {
    (void)x; (void)y;
    if (sn_over) sn_restart();
    else { sn_started = 1; (void)wi; }
}

void snake_key(int wi, int kt, char ch) {
    (void)wi;
    if (sn_over) {
        if (kt == KEY_EVENT_CHAR && (ch == ' ' || ch == 'r' || ch == 'R')) sn_restart();
        return;
    }
    sn_started = 1;
    if (kt == KEY_EVENT_LEFT  && sn_dx ==  0) { sn_ndx = -1; sn_ndy =  0; }
    if (kt == KEY_EVENT_RIGHT && sn_dx ==  0) { sn_ndx =  1; sn_ndy =  0; }
    if (kt == KEY_EVENT_UP    && sn_dy ==  0) { sn_ndx =  0; sn_ndy = -1; }
    if (kt == KEY_EVENT_DOWN  && sn_dy ==  0) { sn_ndx =  0; sn_ndy =  1; }
}

void snake_tick(int wi) {
    (void)wi;
    if (sn_over || !sn_started) return;
    uint32_t now = pit_ticks();
    if (now - sn_last < (uint32_t)SN_SPEED) return;
    sn_last = now;

    sn_dx = sn_ndx; sn_dy = sn_ndy;
    int nx = (int)sn_body[0].x + sn_dx;
    int ny = (int)sn_body[0].y + sn_dy;

    if (nx < 0 || nx >= SN_COLS || ny < 0 || ny >= SN_ROWS) { sn_over = 1; return; }
    for (int i = 1; i < sn_len; i++)
        if (sn_body[i].x == (int8_t)nx && sn_body[i].y == (int8_t)ny)
            { sn_over = 1; return; }

    int ate = (nx == (int)sn_food.x && ny == (int)sn_food.y);
    if (ate) {
        if (sn_len < SN_MAX) sn_len++;
        sn_score++;
        if (sn_score > sn_best) sn_best = sn_score;
        sn_rand_food();
    }
    for (int i = sn_len - 1; i > 0; i--) sn_body[i] = sn_body[i-1];
    sn_body[0].x = (int8_t)nx;
    sn_body[0].y = (int8_t)ny;
}

void snake_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, 0x020703);

    /* Header bar */
    kd_fill(cx, cy, cw, 18, 0x001208);
    kd_str(cx + 4,   cy + 5, "Snake",   KA_HEADFG, 0x001208);
    kd_str(cx + 78,  cy + 5, "Score:",  KA_DIM,    0x001208);
    char b[12];
    kd_itoa(sn_score, b, 12);
    kd_str(cx + 126, cy + 5, b,         KA_BRIGHT, 0x001208);
    kd_str(cx + 178, cy + 5, "Best:",   KA_DIM,    0x001208);
    kd_itoa(sn_best, b, 12);
    kd_str(cx + 220, cy + 5, b,         KA_YELLOW, 0x001208);

    /* Grid */
    int gx = cx + 4, gy = cy + 22;
    kd_fill(gx, gy, SN_COLS * SN_CELL, SN_ROWS * SN_CELL, 0x010502);
    kd_rect(gx - 1, gy - 1, SN_COLS * SN_CELL + 2, SN_ROWS * SN_CELL + 2, 0x1A5533);

    /* Food — pulses between two reds */
    {
        int fx = gx + (int)sn_food.x * SN_CELL + 1;
        int fy = gy + (int)sn_food.y * SN_CELL + 1;
        uint32_t fcol = (pit_ticks() & 8u) ? 0xFF4444u : 0xBB2222u;
        kd_fill(fx, fy, SN_CELL - 2, SN_CELL - 2, fcol);
        kd_fill(fx + 1, fy + 1, SN_CELL - 4, SN_CELL - 4, 0xFF8888u);
    }

    /* Snake body (tail-to-head so head is on top) */
    for (int i = sn_len - 1; i >= 0; i--) {
        uint32_t col = (i == 0) ? 0x66FFAA :
                       (i &  1) ? 0x1A7A44 : 0x22AA55;
        int bx = gx + (int)sn_body[i].x * SN_CELL + 1;
        int by = gy + (int)sn_body[i].y * SN_CELL + 1;
        kd_fill(bx, by, SN_CELL - 2, SN_CELL - 2, col);
    }

    if (!sn_started) {
        kd_fill(cx + 46, cy + 90, 200, 16, 0x001208);
        kd_str(cx + 52, cy + 93, "Press arrow key to start!", KA_DIM, 0x001208);
    }
    if (sn_over) {
        kd_fill(cx + 44, cy + 82, 210, 46, 0x090909);
        kd_rect(cx + 44, cy + 82, 210, 46, KA_RED);
        kd_str(cx + 84, cy + 92,  "GAME OVER!",       KA_RED,  0x090909);
        kd_str(cx + 52, cy + 106, "Space/R or click", KA_DIM,  0x090909);
    }
}
