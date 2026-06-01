/*
 * snake.c — Polished Snake game for SiMPLE OS
 *
 * Standalone WM-based userspace ELF. Legacy _start entry.
 * Green-glass aesthetic matching the SiMPLE OS desktop.
 *
 * Build:
 *   $(USER_CC) -o user/snake.elf user/snake.c user/libc.c
 *
 * Run from SiMPLE OS shell:
 *   run snake.elf
 *
 * Controls:
 *   Arrow keys / WASD — steer
 *   P                 — pause / resume
 *   R                 — restart (game-over screen)
 *   Space / Enter     — begin from title screen
 *   ESC               — quit
 */

#include "wm.h"

/* ── libc forward declarations (satisfied by libc.c) ─────────────── */
void         exit(int code);
unsigned int getticks(void);
int          yield(void);
int          wm_create(int x, int y, int w, int h);
int          wm_destroy(int wid);
int          wm_blit(int wid, unsigned int *buf, int len);
int          wm_event(wm_event_t *ev, int max);
int          wm_setfocus(int wid);

/* ═══════════════════════════════════════════════════════════════════
   Window & grid geometry
   ═══════════════════════════════════════════════════════════════════ */
#define WIN_X    80
#define WIN_Y    40
#define WIN_W   400
#define WIN_H   320
#define CELL     10           /* pixels per grid cell                 */
#define HUD_H    40           /* header bar height                    */
#define PLAY_Y   HUD_H        /* top of play area                     */
#define PLAY_H  (WIN_H - HUD_H)   /* 280 px                          */
#define COLS    (WIN_W / CELL)     /* 40 columns                      */
#define ROWS    (PLAY_H / CELL)    /* 28 rows                         */
#define MAX_SNAKE (COLS * ROWS)    /* 1120 max segments                */

/* ═══════════════════════════════════════════════════════════════════
   Timing (PIT runs at 100 Hz → 1 tick = 10 ms)
   ═══════════════════════════════════════════════════════════════════ */
#define TICK_BASE   12    /* starting interval  (120 ms = ~8 moves/s) */
#define TICK_MIN     3    /* fastest interval   (30 ms = ~33 moves/s) */
#define SPEED_SCORE 50    /* score points before each speed-up step   */

/* ═══════════════════════════════════════════════════════════════════
   PS/2 scancodes
   ═══════════════════════════════════════════════════════════════════ */
#define SC_ESC   0x01
#define SC_W     0x11
#define SC_A     0x1E
#define SC_S     0x1F
#define SC_D     0x20
#define SC_P     0x19
#define SC_R     0x13
#define SC_SPC   0x39
#define SC_ENT   0x1C
#define SC_UP    0x48
#define SC_DOWN  0x50
#define SC_LEFT  0x4B
#define SC_RIGHT 0x4D

/* ═══════════════════════════════════════════════════════════════════
   Colour palette  (0x00RRGGBB)
   Green glass — dark translucent panels, vivid green snake, warm food
   ═══════════════════════════════════════════════════════════════════ */

/* Background / play field */
#define C_WIN_BG       0x020B03U
#define C_HUD_BG       0x04120AU
#define C_HUD_TOP      0x0D3016U   /* glass-shine highlight on HUD     */
#define C_HUD_SEP      0x153D1EU   /* separator line HUD / play area   */
#define C_PLAY_BG      0x040C05U
#define C_GRID_DOT     0x071207U   /* subtle dot at each cell corner   */

/* Wall / border (inner border of play area) */
#define C_WALL_DARK    0x0C3510U
#define C_WALL_MID     0x175C20U
#define C_WALL_BRIGHT  0x22963CU

/* Snake body */
#define C_BODY_BASE    0x149928U
#define C_BODY_LITE    0x18C030U
#define C_BODY_SHINE   0x3AEE58U   /* top-left gloss highlight         */
#define C_BODY_DARK    0x0B6618U   /* bottom-right shadow              */
#define C_BODY_INNER   0x11881FU   /* fill inside the bevel            */

/* Snake head */
#define C_HEAD_BASE    0x1ECC3AU
#define C_HEAD_LITE    0x28FF4CU
#define C_HEAD_SHINE   0x77FFAAU
#define C_EYE_OUTER    0xEEFFEEU
#define C_EYE_PUPIL    0x0A220EU

/* Food (glowing red-orange orb) */
#define C_FOOD_CORE    0xFF2233U
#define C_FOOD_MID     0xFF5566U
#define C_FOOD_SHINE   0xFFAABBU
#define C_FOOD_GLOW    0x6B0011U
#define C_FOOD_DARK    0xAA1122U

/* HUD text */
#define C_TITLE_GLOW   0x1A4422U
#define C_TITLE_MAIN   0x33DD55U
#define C_TITLE_SHINE  0x88FFAAU
#define C_LBL_DIM      0x2A5535U
#define C_LBL_MED      0x3D8850U
#define C_VAL_HI       0x77FF99U
#define C_VAL_MED      0x55CC77U

/* Glass panel (overlays, title) */
#define C_PANEL_BG     0x071A09U
#define C_PANEL_EDGE   0x1E6028U
#define C_PANEL_SHINE  0x2E9040U
#define C_PANEL_INNER  0x0B2410U

/* Overlay text */
#define C_OVR_HI       0xCCFFDDU
#define C_OVR_MED      0x77BB88U
#define C_OVR_DIM      0x3A6644U
#define C_GAMEOVER_RED 0xFF3344U
#define C_GAMEOVER_SHN 0xFF8899U
#define C_PAUSE_CLR    0x55EEDDU

/* ═══════════════════════════════════════════════════════════════════
   8×8 bitmap font (IBM VGA style, MSB = left pixel)
   Only chars we actually use are non-zero.
   ═══════════════════════════════════════════════════════════════════ */
static const unsigned char F8[128][8] = {
    [' ']={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!']={0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['-']={0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    ['.']={0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    [':']={0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    ['/']={0x00,0x03,0x06,0x0C,0x18,0x30,0x60,0x00},
    ['0']={0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    ['1']={0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2']={0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00},
    ['3']={0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['4']={0x0E,0x1E,0x36,0x66,0x7F,0x06,0x06,0x00},
    ['5']={0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    ['6']={0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    ['7']={0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    ['8']={0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    ['9']={0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    ['A']={0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    ['B']={0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    ['C']={0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    ['D']={0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    ['E']={0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    ['F']={0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    ['G']={0x3C,0x66,0x60,0x60,0x6E,0x66,0x3C,0x00},
    ['H']={0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    ['I']={0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['J']={0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00},
    ['K']={0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    ['L']={0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    ['M']={0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    ['N']={0x66,0x76,0x7E,0x6E,0x66,0x66,0x66,0x00},
    ['O']={0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    ['P']={0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    ['Q']={0x3C,0x66,0x66,0x66,0x6E,0x3C,0x06,0x00},
    ['R']={0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},
    ['S']={0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    ['T']={0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    ['U']={0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    ['V']={0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    ['W']={0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    ['X']={0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    ['Y']={0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    ['Z']={0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
};

/* ═══════════════════════════════════════════════════════════════════
   Game state
   ═══════════════════════════════════════════════════════════════════ */
typedef enum { DIR_UP=0, DIR_DOWN=1, DIR_LEFT=2, DIR_RIGHT=3 } dir_t;
typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED, ST_GAMEOVER      } gstate_t;

/* Pixel back-buffer — 400×320×4 = 512 000 bytes (static BSS) */
static unsigned int pixels[WIN_W * WIN_H];

/* Snake body: circular buffer of grid coords */
static short snk_x[MAX_SNAKE];
static short snk_y[MAX_SNAKE];
static int   snk_head;      /* index of head cell               */
static int   snk_len;       /* current length (segments)        */

static int      food_x, food_y;
static gstate_t gstate;
static dir_t    cur_dir, next_dir;
static int      score, hi_score;
static unsigned int last_move_tick;
static int      move_interval;
static unsigned int anim_tick;  /* general animation counter     */

/* ═══════════════════════════════════════════════════════════════════
   PRNG (xorshift32)
   ═══════════════════════════════════════════════════════════════════ */
static unsigned int rng = 0xCAFEBABEU;
static unsigned int rand_next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* ═══════════════════════════════════════════════════════════════════
   Pixel primitives
   ═══════════════════════════════════════════════════════════════════ */
static inline void pset(int x, int y, unsigned int c) {
    if ((unsigned)x < WIN_W && (unsigned)y < WIN_H)
        pixels[y * WIN_W + x] = c;
}
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    int x2 = x + w, y2 = y + h;
    if (x  <  0)    x  = 0;
    if (y  <  0)    y  = 0;
    if (x2 > WIN_W) x2 = WIN_W;
    if (y2 > WIN_H) y2 = WIN_H;
    for (int py = y; py < y2; py++)
        for (int px = x; px < x2; px++)
            pixels[py * WIN_W + px] = c;
}

/* Lerp two colours, t=0→ca, t=255→cb */
static unsigned int clr_mix(unsigned int ca, unsigned int cb, int t) {
    int s = 255 - t;
    unsigned int r = ((ca>>16&0xFF)*s + (cb>>16&0xFF)*t) >> 8;
    unsigned int g = ((ca>> 8&0xFF)*s + (cb>> 8&0xFF)*t) >> 8;
    unsigned int b = ((ca    &0xFF)*s + (cb    &0xFF)*t) >> 8;
    return (r<<16)|(g<<8)|b;
}

/* Draw single pixel-glyph (8×8) at (px,py), scale sx×sy */
static void draw_glyph(int px, int py, unsigned char ch,
                       unsigned int fg, int scale) {
    if (ch >= 128) return;
    for (int row = 0; row < 8; row++) {
        unsigned char bits = F8[(unsigned)ch][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80u >> col)) {
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        pset(px + col*scale + dx,
                             py + row*scale + dy, fg);
            }
        }
    }
}

/* Draw NUL-terminated string; returns width in pixels */
static int draw_str(int px, int py, const char *s,
                    unsigned int fg, int scale) {
    int x0 = px;
    for (; *s; s++, px += (8*scale + scale))
        draw_glyph(px, py, (unsigned char)*s, fg, scale);
    return px - x0;
}

/* Centred string helper */
static void draw_str_cx(int cy, const char *s,
                        unsigned int fg, int scale) {
    int len = 0;
    for (const char *p = s; *p; p++) len++;
    int w = len * (8*scale + scale);
    draw_str((WIN_W - w) / 2, cy, s, fg, scale);
}

/* Convert non-negative int to decimal string, returns pointer to end */
static void int_to_str(int v, char *buf) {
    if (v == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[12]; int i = 0;
    while (v > 0) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    for (int k = i-1; k >= 0; k--) buf[j++] = tmp[k];
    buf[j] = '\0';
}

/* ═══════════════════════════════════════════════════════════════════
   Glass panel helper
   Draws a bevelled, glass-look rectangle.
   ═══════════════════════════════════════════════════════════════════ */
static void draw_glass_panel(int x, int y, int w, int h) {
    /* Fill body */
    fill_rect(x, y, w, h, C_PANEL_BG);
    /* Gloss: lighter top quarter */
    fill_rect(x+1, y+1, w-2, h/4, C_PANEL_INNER);
    /* Outer border */
    fill_rect(x,     y,     w, 1, C_PANEL_SHINE);   /* top bright   */
    fill_rect(x,     y,     1, h, C_PANEL_EDGE);    /* left         */
    fill_rect(x+w-1, y,     1, h, C_PANEL_EDGE);    /* right        */
    fill_rect(x,     y+h-1, w, 1, C_WALL_DARK);     /* bottom dark  */
    /* Inner highlight line */
    fill_rect(x+1,   y+1,   w-2, 1, C_PANEL_SHINE);
}

/* ═══════════════════════════════════════════════════════════════════
   Grid dots
   ═══════════════════════════════════════════════════════════════════ */
static void draw_grid(void) {
    for (int row = 0; row <= ROWS; row++) {
        int py = PLAY_Y + row * CELL;
        for (int col = 0; col <= COLS; col++) {
            int px = col * CELL;
            pset(px, py, C_GRID_DOT);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Wall border (1-cell inset, drawn as coloured edge pixels)
   ═══════════════════════════════════════════════════════════════════ */
static void draw_wall(void) {
    /* Bottom separator */
    fill_rect(0, PLAY_Y, WIN_W, 2, C_WALL_DARK);
    /* Top bright line on separator */
    fill_rect(0, PLAY_Y, WIN_W, 1, C_WALL_BRIGHT);

    /* Outer play-area frame (2 px) */
    fill_rect(0,       PLAY_Y+2, 2,     PLAY_H-2, C_WALL_MID);
    fill_rect(WIN_W-2, PLAY_Y+2, 2,     PLAY_H-2, C_WALL_MID);
    fill_rect(0,       WIN_H-2,  WIN_W, 2,         C_WALL_MID);
    /* Inner bright edge */
    fill_rect(2,       PLAY_Y+3, 1,     PLAY_H-5, C_WALL_BRIGHT);
    fill_rect(WIN_W-3, PLAY_Y+3, 1,     PLAY_H-5, C_WALL_BRIGHT);
}

/* ═══════════════════════════════════════════════════════════════════
   Snake segment rendering
   ═══════════════════════════════════════════════════════════════════ */
static void draw_segment(int gx, int gy, int is_tail) {
    int px = gx * CELL + 1;
    int py = PLAY_Y + gy * CELL + 1;
    int sz = CELL - 2;      /* 8×8 interior */

    /* Base fill — slightly dimmer for tail */
    unsigned int base = is_tail ? C_BODY_BASE : C_BODY_LITE;
    fill_rect(px, py, sz, sz, base);

    /* Top-left gloss highlight (2 rows + 2 cols) */
    fill_rect(px,    py,    sz,   1, C_BODY_SHINE);
    fill_rect(px,    py,    1,    sz, C_BODY_SHINE);
    /* Inner top row (row 1) slightly bright */
    fill_rect(px+1,  py+1,  sz-2, 1, C_BODY_INNER);

    /* Bottom-right shadow */
    fill_rect(px,    py+sz-1, sz,   1, C_BODY_DARK);
    fill_rect(px+sz-1, py,    1,    sz, C_BODY_DARK);
}

static void draw_head(int gx, int gy, dir_t dir) {
    int px = gx * CELL + 1;
    int py = PLAY_Y + gy * CELL + 1;
    int sz = CELL - 2;

    /* Brighter head fill */
    fill_rect(px, py, sz, sz, C_HEAD_BASE);
    /* Gloss */
    fill_rect(px,    py,    sz,   1, C_HEAD_SHINE);
    fill_rect(px,    py,    1,    sz, C_HEAD_SHINE);
    fill_rect(px+1,  py+1,  sz-2, 1, C_HEAD_LITE);
    /* Shadow */
    fill_rect(px,    py+sz-1, sz,   1, C_BODY_DARK);
    fill_rect(px+sz-1, py,    1,    sz, C_BODY_DARK);

    /* Eyes — 2 px dots placed according to direction */
    int ex1, ey1, ex2, ey2;
    switch (dir) {
        case DIR_RIGHT:
            ex1 = px+sz-2; ey1 = py+2;
            ex2 = px+sz-2; ey2 = py+sz-3;
            break;
        case DIR_LEFT:
            ex1 = px+1;    ey1 = py+2;
            ex2 = px+1;    ey2 = py+sz-3;
            break;
        case DIR_UP:
            ex1 = px+2;    ey1 = py+1;
            ex2 = px+sz-3; ey2 = py+1;
            break;
        default: /* DOWN */
            ex1 = px+2;    ey1 = py+sz-2;
            ex2 = px+sz-3; ey2 = py+sz-2;
            break;
    }
    pset(ex1, ey1, C_EYE_OUTER); pset(ex1+1, ey1, C_EYE_OUTER);
    pset(ex2, ey2, C_EYE_OUTER); pset(ex2+1, ey2, C_EYE_OUTER);
    pset(ex1, ey1, C_EYE_PUPIL);
    pset(ex2, ey2, C_EYE_PUPIL);
}

/* ═══════════════════════════════════════════════════════════════════
   Food orb rendering  (pulsating glow radius based on anim_tick)
   ═══════════════════════════════════════════════════════════════════ */
static void draw_food(int gx, int gy) {
    int cx = gx * CELL + CELL/2;
    int cy = PLAY_Y + gy * CELL + CELL/2;

    /* Outer glow: 5×5 faded ring */
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 <= 16 && d2 > 9) {
                unsigned int t = (unsigned int)(anim_tick & 0x1F);
                int pulse = (int)(t < 16 ? t : 32-t);  /* 0..16 */
                unsigned int c = clr_mix(C_FOOD_GLOW, C_FOOD_DARK, pulse*15);
                pset(cx+dx, cy+dy, c);
            }
        }
    }
    /* Core 3×3 orb */
    pset(cx-1, cy-1, C_FOOD_MID);   pset(cx, cy-1, C_FOOD_MID);   pset(cx+1, cy-1, C_FOOD_DARK);
    pset(cx-1, cy,   C_FOOD_MID);   pset(cx, cy,   C_FOOD_CORE);  pset(cx+1, cy,   C_FOOD_CORE);
    pset(cx-1, cy+1, C_FOOD_DARK);  pset(cx, cy+1, C_FOOD_CORE);  pset(cx+1, cy+1, C_FOOD_DARK);
    /* Specular highlight */
    pset(cx-1, cy-1, C_FOOD_SHINE);
}

/* ═══════════════════════════════════════════════════════════════════
   HUD  (top 40 px)
   ═══════════════════════════════════════════════════════════════════ */
static void draw_hud(void) {
    /* Background panel */
    fill_rect(0, 0, WIN_W, HUD_H, C_HUD_BG);
    /* Gloss top line */
    fill_rect(0, 0, WIN_W, 1, C_HUD_TOP);
    /* Bottom separator */
    fill_rect(0, HUD_H-1, WIN_W, 1, C_HUD_SEP);

    /* Title — centred, medium scale */
    int tx = (WIN_W - 5*(8*2+2)) / 2;
    /* Shadow offset */
    draw_str(tx+1, 13, "SNAKE", C_TITLE_GLOW, 2);
    draw_str(tx,   12, "SNAKE", C_TITLE_MAIN, 2);

    /* Score — left side */
    char num[12];
    draw_str(8, 8,  "SCORE",  C_LBL_DIM, 1);
    int_to_str(score, num);
    draw_str(8, 22, num, C_VAL_HI, 1);

    /* High score — right side */
    draw_str(WIN_W-60, 8,  "BEST",   C_LBL_DIM, 1);
    int_to_str(hi_score, num);
    draw_str(WIN_W-60, 22, num, C_VAL_MED, 1);
}

/* ═══════════════════════════════════════════════════════════════════
   Full scene render
   ═══════════════════════════════════════════════════════════════════ */
static void render_game(void) {
    /* Play area background */
    fill_rect(0, PLAY_Y, WIN_W, PLAY_H, C_PLAY_BG);
    draw_grid();
    draw_wall();

    /* Food */
    draw_food(food_x, food_y);

    /* Snake body (tail → head-1, so we overdraw head last) */
    for (int i = snk_len - 1; i >= 1; i--) {
        int idx = (snk_head - i + MAX_SNAKE) % MAX_SNAKE;
        int is_tail = (i == snk_len - 1);
        draw_segment(snk_x[idx], snk_y[idx], is_tail);
    }
    /* Snake head */
    draw_head(snk_x[snk_head], snk_y[snk_head], cur_dir);

    draw_hud();
}

/* Darken the play area by halving each channel (50% darkening) */
static void darken_play_area(void) {
    for (int py = PLAY_Y; py < WIN_H; py++) {
        unsigned int *row = pixels + py * WIN_W;
        for (int px = 0; px < WIN_W; px++)
            row[px] = (row[px] >> 1) & 0x7F7F7FU;
    }
}

/* ─── Pause overlay ──────────────────────────────────────────────── */
static void render_paused(void) {
    render_game();
    darken_play_area();
    /* Glass panel */
    int pw = 200, ph = 80;
    int ppx = (WIN_W-pw)/2, ppy = PLAY_Y + (PLAY_H-ph)/2;
    draw_glass_panel(ppx, ppy, pw, ph);
    draw_str_cx(ppy+14, "PAUSED",       C_PAUSE_CLR, 2);
    draw_str_cx(ppy+46, "P TO RESUME",  C_OVR_MED, 1);
    draw_hud();
}

/* ─── Game-over overlay ──────────────────────────────────────────── */
static void render_gameover(void) {
    render_game();
    darken_play_area();
    int pw = 230, ph = 110;
    int ppx = (WIN_W-pw)/2, ppy = PLAY_Y + (PLAY_H-ph)/2;
    draw_glass_panel(ppx, ppy, pw, ph);

    /* "GAME OVER" header with red/glow */
    int hx = (WIN_W - 9*(8+1)) / 2;  /* 9 chars × 9 px wide */
    draw_str(hx+1, ppy+10, "GAME OVER", C_GAMEOVER_SHN, 1);
    draw_str(hx,   ppy+ 9, "GAME OVER", C_GAMEOVER_RED, 1);

    char num[12];
    /* Score row */
    int_to_str(score, num);
    draw_str(ppx+12, ppy+28, "SCORE:", C_OVR_MED, 1);
    draw_str(ppx+70, ppy+28, num,      C_VAL_HI, 1);
    /* Hi-score row */
    int_to_str(hi_score, num);
    draw_str(ppx+12, ppy+42, "BEST: ", C_OVR_MED, 1);
    draw_str(ppx+70, ppy+42, num,      C_VAL_HI, 1);

    draw_str_cx(ppy+62,  "R - RESTART",   C_OVR_MED, 1);
    draw_str_cx(ppy+76,  "ESC - QUIT",    C_OVR_DIM, 1);
    draw_hud();
}

/* ─── Title / start screen ───────────────────────────────────────── */
static void render_title(void) {
    fill_rect(0, 0, WIN_W, WIN_H, C_WIN_BG);

    /* Decorative grid background */
    for (int row = 0; row <= ROWS+4; row++)
        for (int col = 0; col <= COLS; col++)
            pset(col*CELL, row*CELL, C_GRID_DOT);

    /* A decorative serpentine snake (static demo) */
    static const unsigned char demo[] = {
        20,14, 19,14, 18,14, 17,14, 16,14, 15,14,
        14,14, 13,14, 13,15, 13,16, 14,16, 15,16,
        16,16, 17,16, 18,16, 19,16, 19,17, 19,18,
        18,18, 17,18, 16,18, 15,18, 14,18, 13,18
    };
    int n = (int)(sizeof(demo)/2);  /* each segment = 2 bytes: (gx, gy) */
    for (int i = n-1; i >= 1; i--)
        draw_segment(demo[i*2], demo[i*2+1], i==n-1);
    draw_head(demo[0], demo[1], DIR_LEFT);

    /* Draw a food orb near the snake */
    draw_food(21, 14);

    /* Title glass panel */
    int pw = 300, ph = 140;
    int ppx = (WIN_W-pw)/2, ppy = (WIN_H-ph)/2 - 20;
    draw_glass_panel(ppx, ppy, pw, ph);

    /* "SNAKE" large title */
    int pulse = (int)(anim_tick & 0x3F);
    if (pulse > 32) pulse = 64 - pulse;  /* 0..32 triangle wave */
    unsigned int title_c = clr_mix(C_TITLE_MAIN, C_TITLE_SHINE, pulse*7);
    int tx = (WIN_W - 5*(16+2)) / 2;
    draw_str(tx+1, ppy+12, "SNAKE", C_TITLE_GLOW,  2);
    draw_str(tx,   ppy+11, "SNAKE", title_c,        2);

    /* Divider line */
    fill_rect(ppx+20, ppy+46, pw-40, 1, C_PANEL_EDGE);

    /* Instructions */
    draw_str_cx(ppy+ 55, "SPACE TO BEGIN",    C_OVR_HI,  1);
    draw_str_cx(ppy+ 71, "WASD / ARROWS",     C_OVR_MED, 1);
    draw_str_cx(ppy+ 85, "P - PAUSE",         C_OVR_MED, 1);
    draw_str_cx(ppy+ 99, "R - RESTART",       C_OVR_DIM, 1);
    draw_str_cx(ppy+113, "ESC - QUIT",        C_OVR_DIM, 1);

    /* Bottom label */
    draw_str_cx(WIN_H-14, "SIMPLE OS", C_LBL_DIM, 1);
}

/* ═══════════════════════════════════════════════════════════════════
   Game logic
   ═══════════════════════════════════════════════════════════════════ */
static int cell_in_snake(int gx, int gy) {
    for (int i = 0; i < snk_len; i++) {
        int idx = (snk_head - i + MAX_SNAKE) % MAX_SNAKE;
        if (snk_x[idx] == gx && snk_y[idx] == gy) return 1;
    }
    return 0;
}

static void spawn_food(void) {
    /* Seed RNG with current ticks for variety */
    rng ^= getticks();
    int tries = 0;
    do {
        food_x = (int)(rand_next() % (unsigned)COLS);
        food_y = (int)(rand_next() % (unsigned)ROWS);
        tries++;
    } while (cell_in_snake(food_x, food_y) && tries < 200);
}

static void init_game(void) {
    /* Reset snake: start at centre heading right, length 4 */
    snk_len  = 4;
    snk_head = 3;
    int sx0 = COLS/2, sy0 = ROWS/2;
    for (int i = 0; i < snk_len; i++) {
        snk_x[i] = (short)(sx0 - (snk_len-1-i));
        snk_y[i] = (short)sy0;
    }

    cur_dir  = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    score    = 0;
    move_interval = TICK_BASE;
    last_move_tick = getticks();

    spawn_food();
}

/* Returns 1 if snake died, 0 if alive */
static int step_snake(void) {
    int hx = snk_x[snk_head];
    int hy = snk_y[snk_head];

    /* Apply buffered direction (prevent 180-degree reversal) */
    dir_t d = next_dir;
    if ((d == DIR_UP    && cur_dir == DIR_DOWN)  ||
        (d == DIR_DOWN  && cur_dir == DIR_UP)    ||
        (d == DIR_LEFT  && cur_dir == DIR_RIGHT) ||
        (d == DIR_RIGHT && cur_dir == DIR_LEFT)) {
        d = cur_dir;  /* ignore illegal turn */
    }
    cur_dir = d;

    int nx = hx, ny = hy;
    switch (cur_dir) {
        case DIR_UP:    ny--; break;
        case DIR_DOWN:  ny++; break;
        case DIR_LEFT:  nx--; break;
        case DIR_RIGHT: nx++; break;
    }

    /* Wall collision (play-area boundary inset by wall pixels = 1 cell) */
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) return 1;

    /* Self-collision (check all but the tail which will move away) */
    for (int i = 0; i < snk_len - 1; i++) {
        int idx = (snk_head - i + MAX_SNAKE) % MAX_SNAKE;
        if (snk_x[idx] == nx && snk_y[idx] == ny) return 1;
    }

    /* Advance head */
    snk_head = (snk_head + 1) % MAX_SNAKE;
    snk_x[snk_head] = (short)nx;
    snk_y[snk_head] = (short)ny;

    /* Ate food? */
    if (nx == food_x && ny == food_y) {
        snk_len++;
        if (snk_len > MAX_SNAKE) snk_len = MAX_SNAKE;
        score += 10;
        if (score > hi_score) hi_score = score;
        /* Speed up */
        move_interval = TICK_BASE - score / SPEED_SCORE;
        if (move_interval < TICK_MIN) move_interval = TICK_MIN;
        spawn_food();
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
   Entry point
   ═══════════════════════════════════════════════════════════════════ */
void _start(void) {
    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) exit(1);
    wm_setfocus(wid);

    gstate    = ST_TITLE;
    hi_score  = 0;
    anim_tick = 0;
    init_game();

    /* Initial render */
    render_title();
    wm_blit(wid, pixels, WIN_W * WIN_H * 4);

    unsigned int last_frame = getticks();
    wm_event_t ev;

    for (;;) {
        /* ── Drain all pending events ──────────────────────────── */
        int r;
        while ((r = wm_event(&ev, (int)sizeof(ev))) > 0) {
            if (r == WM_EV_CLOSE) {
                wm_destroy(wid);
                exit(0);
            }
            if (r != WM_EV_KEY_DOWN) continue;

            int sc = ev.x & 0xFF;

            /* Global: ESC always quits */
            if (sc == SC_ESC) {
                wm_destroy(wid);
                exit(0);
            }

            switch (gstate) {
            case ST_TITLE:
                if (sc == SC_SPC || sc == SC_ENT || sc == SC_R) {
                    init_game();
                    gstate = ST_PLAYING;
                }
                break;

            case ST_PLAYING:
                /* Direction keys */
                if (sc == SC_UP    || sc == SC_W) next_dir = DIR_UP;
                if (sc == SC_DOWN  || sc == SC_S) next_dir = DIR_DOWN;
                if (sc == SC_LEFT  || sc == SC_A) next_dir = DIR_LEFT;
                if (sc == SC_RIGHT || sc == SC_D) next_dir = DIR_RIGHT;
                /* Pause */
                if (sc == SC_P) {
                    gstate = ST_PAUSED;
                }
                break;

            case ST_PAUSED:
                if (sc == SC_P || sc == SC_SPC) {
                    gstate = ST_PLAYING;
                    last_move_tick = getticks();  /* reset timer, no jump */
                }
                break;

            case ST_GAMEOVER:
                if (sc == SC_R || sc == SC_SPC || sc == SC_ENT) {
                    init_game();
                    gstate = ST_PLAYING;
                }
                break;
            }
        }

        /* ── Tick-driven logic & rendering ─────────────────────── */
        unsigned int now = getticks();
        anim_tick = now;       /* animation uses raw tick counter  */

        /* Render on a periodic basis even in non-playing states,
         * so the title glow and food pulse continue animating.    */
        int frame_due = (now - last_frame) >= 4; /* ~25 fps max   */

        if (gstate == ST_PLAYING) {
            int move_due = (now - last_move_tick) >= (unsigned)move_interval;
            if (move_due) {
                int died = step_snake();
                last_move_tick = now;
                if (died) {
                    gstate = ST_GAMEOVER;
                    if (score > hi_score) hi_score = score;
                }
            }
            if (frame_due) {
                render_game();
                wm_blit(wid, pixels, WIN_W * WIN_H * 4);
                last_frame = now;
            }
        } else if (frame_due) {
            /* Animate title / overlays at lower rate */
            switch (gstate) {
            case ST_TITLE:    render_title();    break;
            case ST_PAUSED:   render_paused();   break;
            case ST_GAMEOVER: render_gameover(); break;
            default: break;
            }
            wm_blit(wid, pixels, WIN_W * WIN_H * 4);
            last_frame = now;
        }

        yield();   /* cooperate with other processes */
    }
}
