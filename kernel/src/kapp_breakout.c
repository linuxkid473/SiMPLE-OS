#include "kapp.h"
#include "pit.h"
#include "string.h"

/* Breakout — mouse-controlled paddle, fixed-point ball (units = 1/4 pixel) */
#define BRK_W    352
#define BRK_H    218
#define BRK_PAD  4
#define PDL_W    64
#define PDL_H    8
#define PDL_Y    (BRK_H - 20)
#define BALL_R   4
#define BCOLS    8
#define BROWS    5
#define BW       ((BRK_W - 2 * BRK_PAD) / BCOLS)   /* 43 px */
#define BH       14

static int brk_pdl_x;
static int brk_bx, brk_by;   /* ball centre, ×4 */
static int brk_vx, brk_vy;   /* ball velocity, ×4 */
static int brk_bricks[BROWS][BCOLS];
static int brk_score, brk_lives, brk_over, brk_won;
static uint32_t brk_last;

static void brk_reset_ball(void) {
    brk_bx = (BRK_W / 2) * 4;
    brk_by = (PDL_Y - 30) * 4;
    brk_vx =  3;
    brk_vy = -5;
}

void breakout_create(int wi) {
    (void)wi;
    brk_pdl_x = BRK_W / 2 - PDL_W / 2;
    brk_score = 0; brk_lives = 3; brk_over = 0; brk_won = 0;
    for (int r = 0; r < BROWS; r++)
        for (int c = 0; c < BCOLS; c++)
            brk_bricks[r][c] = 1;
    brk_reset_ball();
    brk_last = pit_ticks();
}
void breakout_destroy(int wi) { (void)wi; }

void breakout_key(int wi, int kt, char ch) {
    (void)kt;
    if (brk_over || brk_won)
        if (ch == ' ' || ch == 'r' || ch == 'R') breakout_create(wi);
}

void breakout_click(int wi, int x, int y) {
    (void)x; (void)y;
    if (brk_over || brk_won) breakout_create(wi);
}

void breakout_mouse(int wi, int x, int y, int btn) {
    (void)wi; (void)y; (void)btn;
    brk_pdl_x = x - PDL_W / 2;
    if (brk_pdl_x < 0) brk_pdl_x = 0;
    if (brk_pdl_x + PDL_W > BRK_W) brk_pdl_x = BRK_W - PDL_W;
}

void breakout_tick(int wi) {
    (void)wi;
    if (brk_over || brk_won) return;

    /* Cap to ~60 fps to keep speed consistent */
    uint32_t now = pit_ticks();
    if (now == brk_last) return;
    brk_last = now;

    int bx = brk_bx + brk_vx;
    int by = brk_by + brk_vy;
    int r4 = BALL_R * 4;

    /* Left / right walls */
    if (bx - r4 < 0)             { bx = r4;          brk_vx = -brk_vx; }
    if (bx + r4 > BRK_W * 4)     { bx = BRK_W*4-r4;  brk_vx = -brk_vx; }
    /* Top wall */
    if (by - r4 < BRK_PAD * 4)   { by = BRK_PAD*4+r4; brk_vy = -brk_vy; }

    /* Paddle */
    int px4 = brk_pdl_x * 4, py4 = PDL_Y * 4;
    if (brk_vy > 0 && by + r4 >= py4 && by - r4 <= py4 + PDL_H*4
                    && bx >= px4 && bx <= px4 + PDL_W*4) {
        by = py4 - r4;
        brk_vy = -(brk_vy < 0 ? -brk_vy : brk_vy);  /* ensure upward */
        if (brk_vy == 0) brk_vy = -5;
        /* Spin: deflect based on hit offset from paddle centre */
        int hit_off = (bx - (px4 + PDL_W*2)) / 12;
        brk_vx += hit_off;
        if (brk_vx >  10) brk_vx =  10;
        if (brk_vx < -10) brk_vx = -10;
        if (brk_vx ==  0) brk_vx =  2;
    }

    /* Bottom — lose a life */
    if (by + r4 > (BRK_H + 8) * 4) {
        brk_lives--;
        if (brk_lives <= 0) { brk_over = 1; return; }
        brk_reset_ball();
        return;
    }

    /* Brick collisions */
    int bxi = bx / 4, byi = by / 4;
    for (int row = 0; row < BROWS; row++) {
        int ry = BRK_PAD + 22 + row * (BH + 2);
        for (int col = 0; col < BCOLS; col++) {
            if (!brk_bricks[row][col]) continue;
            int rx = BRK_PAD + col * BW;
            if (bxi >= rx && bxi < rx + BW && byi >= ry && byi < ry + BH) {
                brk_bricks[row][col] = 0;
                brk_score += (BROWS - row) * 10;
                brk_vy = -brk_vy;
                /* Win check */
                int left = 0;
                for (int r2 = 0; r2 < BROWS; r2++)
                    for (int c2 = 0; c2 < BCOLS; c2++)
                        if (brk_bricks[r2][c2]) left++;
                if (!left) { brk_won = 1; return; }
                goto done_bricks;
            }
        }
    }
done_bricks:
    brk_bx = bx; brk_by = by;
}

static const uint32_t brk_row_colors[BROWS] = {
    0xFF4444u, 0xFF8844u, 0xFFDD44u, 0x44FF88u, 0x44AAFFu
};

void breakout_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, 0x010208);

    /* Header */
    kd_fill(cx, cy, cw, 18, 0x001208);
    kd_str(cx + 4,   cy + 5, "Breakout",  KA_HEADFG, 0x001208);
    kd_str(cx + 90,  cy + 5, "Score:",    KA_DIM,    0x001208);
    char b[12];
    kd_itoa(brk_score, b, 12);
    kd_str(cx + 138, cy + 5, b,            KA_BRIGHT, 0x001208);
    kd_str(cx + 210, cy + 5, "Lives:",    KA_DIM,    0x001208);
    for (int i = 0; i < 3; i++) {
        uint32_t lc = (i < brk_lives) ? 0xFF4444u : 0x330A0Au;
        kd_fill(cx + 258 + i * 14, cy + 6, 10, 8, lc);
    }
    kd_str(cx + 310, cy + 5, "Move mouse!", KA_DIM, 0x001208);

    /* Play field */
    int ox = cx + 2, oy = cy + 20;
    kd_rect(ox - 1, oy - 1, BRK_W + 2, BRK_H + 2, 0x1A3355);

    /* Bricks */
    for (int row = 0; row < BROWS; row++) {
        uint32_t col = brk_row_colors[row];
        for (int c = 0; c < BCOLS; c++) {
            if (!brk_bricks[row][c]) continue;
            int bx = ox + BRK_PAD + c * BW;
            int by = oy + BRK_PAD + 22 + row * (BH + 2);
            kd_fill(bx, by, BW - 2, BH, col);
            kd_hline(bx, by, BW - 2, 0xFFFFFF);       /* top gloss */
            kd_hline(bx, by + BH - 1, BW - 2, 0x222222); /* bot shadow */
        }
    }

    /* Paddle */
    int px = ox + brk_pdl_x, py = oy + PDL_Y;
    kd_fill(px, py, PDL_W, PDL_H, 0x44FF88u);
    kd_fill(px, py, PDL_W, 2,     0xAAFFCCu);
    kd_hline(px, py + PDL_H - 1, PDL_W, 0x115522u);

    /* Ball */
    int bsx = ox + brk_bx / 4 - BALL_R;
    int bsy = oy + brk_by / 4 - BALL_R;
    kd_fill(bsx, bsy, BALL_R * 2, BALL_R * 2, 0xFFFFFFu);
    kd_fill(bsx, bsy, 2, 2, 0xCCCCCCu);  /* gloss dot */

    if (brk_over) {
        kd_fill(cx + 76, cy + 96, 210, 46, 0x080808);
        kd_rect(cx + 76, cy + 96, 210, 46, KA_RED);
        kd_str(cx + 114, cy + 106, "GAME OVER!",    KA_RED, 0x080808);
        kd_str(cx +  88, cy + 120, "Click/R to restart", KA_DIM, 0x080808);
    }
    if (brk_won) {
        kd_fill(cx + 76, cy + 96, 210, 46, 0x080808);
        kd_rect(cx + 76, cy + 96, 210, 46, 0x44FF88u);
        kd_str(cx + 104, cy + 106, "CLEARED!  +LIFE",  0x44FF88u, 0x080808);
        kd_str(cx +  88, cy + 120, "Click/R to restart", KA_DIM, 0x080808);
    }
}
