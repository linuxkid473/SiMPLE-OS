#include "kapp.h"
#include "pit.h"
#include "string.h"

/* Pong — mouse controls right paddle; left is AI */
#define PG_W   356
#define PG_H   210
#define PDL_H   44
#define PDL_W    8
#define BALL_SZ  6
#define WIN_SC   7

static int pg_bx, pg_by;   /* ball centre ×4 */
static int pg_vx, pg_vy;
static int pg_lpad, pg_rpad;  /* top-of-paddle Y */
static int pg_lsc,  pg_rsc;
static int pg_over;
static uint32_t pg_last;

static void pg_launch(int dir) {
    pg_bx = (PG_W / 2) * 4;
    pg_by = (PG_H / 2) * 4;
    pg_vx = dir * 6;
    pg_vy = 4;
}

void pong_create(int wi) {
    (void)wi;
    pg_lpad = pg_rpad = PG_H / 2 - PDL_H / 2;
    pg_lsc = pg_rsc = 0; pg_over = 0;
    pg_launch(1);
    pg_last = pit_ticks();
}
void pong_destroy(int wi) { (void)wi; }

void pong_key(int wi, int kt, char ch) {
    (void)kt;
    if (pg_over && (ch == ' ' || ch == 'r' || ch == 'R')) pong_create(wi);
}

void pong_click(int wi, int x, int y) {
    (void)x; (void)y;
    if (pg_over) pong_create(wi);
}

void pong_mouse(int wi, int x, int y, int btn) {
    (void)wi; (void)x; (void)btn;
    /* y is client-relative; subtract header */
    pg_rpad = y - 18 - PDL_H / 2;
    if (pg_rpad < 0)              pg_rpad = 0;
    if (pg_rpad + PDL_H > PG_H)  pg_rpad = PG_H - PDL_H;
}

void pong_tick(int wi) {
    (void)wi;
    if (pg_over) return;
    uint32_t now = pit_ticks();
    if (now == pg_last) return;
    pg_last = now;

    int bx = pg_bx + pg_vx;
    int by = pg_by + pg_vy;

    /* Top / bottom */
    if (by < 0)         { by = 0;       pg_vy = -pg_vy; }
    if (by > PG_H * 4)  { by = PG_H*4;  pg_vy = -pg_vy; }

    /* AI tracks ball with slight lag */
    int ai_c  = pg_lpad + PDL_H / 2;
    int ball_c = by / 4;
    if (ai_c < ball_c - 3) pg_lpad += 4;
    else if (ai_c > ball_c + 3) pg_lpad -= 4;
    if (pg_lpad < 0)             pg_lpad = 0;
    if (pg_lpad + PDL_H > PG_H)  pg_lpad = PG_H - PDL_H;

    /* Left paddle */
    int lx4 = (10 + PDL_W) * 4;
    if (pg_vx < 0 && bx <= lx4) {
        int py4 = pg_lpad * 4, ph4 = PDL_H * 4;
        if (by + BALL_SZ*4/2 >= py4 && by - BALL_SZ*4/2 <= py4 + ph4) {
            bx = lx4; pg_vx = -pg_vx;
            pg_vy += (by - py4 - ph4/2) / 20;
        }
    }

    /* Right paddle */
    int rx4 = (PG_W - 10 - PDL_W) * 4;
    if (pg_vx > 0 && bx >= rx4) {
        int py4 = pg_rpad * 4, ph4 = PDL_H * 4;
        if (by + BALL_SZ*4/2 >= py4 && by - BALL_SZ*4/2 <= py4 + ph4) {
            bx = rx4; pg_vx = -pg_vx;
            pg_vy += (by - py4 - ph4/2) / 20;
        }
    }

    /* Clamp vy */
    if (pg_vy >  9) pg_vy =  9;
    if (pg_vy < -9) pg_vy = -9;

    /* Score */
    if (bx < 0) {
        pg_lsc++;
        if (pg_lsc >= WIN_SC) { pg_over = 1; return; }
        pg_launch(-1); return;
    }
    if (bx > PG_W * 4) {
        pg_rsc++;
        if (pg_rsc >= WIN_SC) { pg_over = 1; return; }
        pg_launch(1); return;
    }

    pg_bx = bx; pg_by = by;
}

void pong_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, 0x010305);

    /* Header */
    kd_fill(cx, cy, cw, 18, 0x001208);
    kd_str(cx + 4,   cy + 5, "Pong",        KA_HEADFG, 0x001208);
    char b[8];
    kd_str(cx + 72,  cy + 5, "AI",          KA_RED,    0x001208);
    kd_itoa(pg_lsc, b, 8);
    kd_str(cx + 92,  cy + 5, b,             KA_BRIGHT, 0x001208);
    kd_str(cx + 108, cy + 5, ":",           KA_DIM,    0x001208);
    kd_itoa(pg_rsc, b, 8);
    kd_str(cx + 116, cy + 5, b,             KA_BRIGHT, 0x001208);
    kd_str(cx + 132, cy + 5, "You",         0x44FF88u, 0x001208);
    kd_str(cx + 230, cy + 5, "Move mouse!", KA_DIM,    0x001208);

    /* Play field */
    int ox = cx + 2, oy = cy + 20;
    kd_rect(ox - 1, oy - 1, PG_W + 2, PG_H + 2, 0x1A3355u);

    /* Centre dashes */
    for (int y = 0; y < PG_H; y += 12)
        kd_fill(ox + PG_W/2 - 1, oy + y, 2, 8, 0x0E2230u);

    /* Paddles */
    kd_fill(ox + 10,              oy + pg_lpad, PDL_W, PDL_H, KA_RED);
    kd_fill(ox + PG_W - 10 - PDL_W, oy + pg_rpad, PDL_W, PDL_H, 0x44FF88u);

    /* Ball */
    int bsx = ox + pg_bx/4 - BALL_SZ/2;
    int bsy = oy + pg_by/4 - BALL_SZ/2;
    kd_fill(bsx, bsy, BALL_SZ, BALL_SZ, 0xFFFFFFu);

    /* Score tick marks along top edge */
    for (int i = 0; i < WIN_SC; i++) {
        uint32_t lc = (i < pg_lsc) ? 0xFF4444u : 0x1A1A1Au;
        uint32_t rc = (i < pg_rsc) ? 0x44FF88u : 0x1A1A1Au;
        kd_fill(ox + 20  + i * 10, oy + 4, 8, 4, lc);
        kd_fill(ox + PG_W - 88 + i * 10, oy + 4, 8, 4, rc);
    }

    if (pg_over) {
        int player_won = pg_rsc >= WIN_SC;
        uint32_t col = player_won ? 0x44FF88u : KA_RED;
        kd_fill(cx + 70, cy + 96, 220, 46, 0x080808);
        kd_rect(cx + 70, cy + 96, 220, 46, col);
        kd_str(cx + 106, cy + 106, player_won ? "YOU WIN!  :D" : "AI WINS!  :(", col, 0x080808);
        kd_str(cx +  86, cy + 120, "Click or R to restart", KA_DIM, 0x080808);
    }
}
