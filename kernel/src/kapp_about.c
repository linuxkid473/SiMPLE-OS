/* kapp_about.c — SiMPLE RACER (OutRun-style endless drive) */
#include "kapp.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define ROAD_HALF    85       /* half road width in world units   */
#define CAM_D        160      /* camera depth for perspective     */
#define SPEED_MAX    280      /* max speed (world units / tick)   */
#define SPEED_IDLE   30       /* background decel per tick        */
#define START_TICKS  3000     /* 30 s at 100 Hz                   */
#define CHECK_TICKS  1500     /* +15 s per checkpoint             */

#define SEG_LEN      256      /* world units per road segment     */
#define NUM_SEGS     64       /* track length before wrap         */
#define SEG_MASK     63

/* Visible depth: how many world units ahead we render */
#define VIS_DEPTH    (CAM_D * 28)

/* Object types encoded in nibbles of seg_obj tables */
#define OBJ_NONE     0
#define OBJ_TREE     1
#define OBJ_SIGN     2
#define OBJ_LAMP     3
#define OBJ_BUSH     4

/* Time-of-day thresholds (distance units before cycling) */
#define TOD_DAY_D    120000u
#define TOD_SUN_D    60000u
#define TOD_NIGHT_D  90000u
#define TOD_TOTAL    (TOD_DAY_D + TOD_SUN_D + TOD_NIGHT_D)

/* ================================================================
 * Track data
 * ================================================================ */

/* Curve: negative = left, positive = right  (-8..+8) */
static const signed char s_curve[NUM_SEGS] = {
     0, 0, 0, 0, 0, 0, 0, 0,   /*  0-7  long straight          */
    -2,-4,-6,-6,-4,-2, 0, 0,   /*  8-15 gentle left sweep      */
     0, 0, 0, 0, 0, 0, 0, 0,   /* 16-23 straight               */
     3, 5, 7, 7, 5, 3, 1, 0,   /* 24-31 sweeping right         */
     0, 0, 0, 0, 0, 0, 0, 0,   /* 32-39 straight               */
    -3,-6,-8,-6,-3, 0, 0, 0,   /* 40-47 sharp left             */
     0, 0, 0, 0, 0, 0, 0, 0,   /* 48-55 straight               */
     4, 7, 8, 7, 4, 2, 0, 0,   /* 56-63 sharp right            */
};

/* Hill: positive = crest (horizon rises), negative = dip (-5..+5) */
static const signed char s_hill[NUM_SEGS] = {
     0, 0, 0, 0, 0, 0, 0, 0,
     2, 4, 5, 4, 2, 0, 0, 0,   /* hill crest                   */
     0, 0, 0, 0, 0, 0, 0, 0,
    -2,-4,-5,-4,-2, 0, 0, 0,   /* valley dip                   */
     0, 0, 0, 0, 0, 0, 0, 0,
     3, 5, 5, 3, 1, 0, 0, 0,   /* rolling hill                 */
     0, 0, 0, 0, 0, 0, 0, 0,
    -3,-5,-5,-3,-1, 0, 0, 0,   /* long descent                 */
};

/* Roadside object masks — one nibble each side per segment
   bits 0-2: object type, bit 3 unused */
static const uint8_t s_obj_left[NUM_SEGS] = {
    OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_BUSH,OBJ_TREE,
    OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_BUSH,OBJ_TREE,OBJ_NONE,OBJ_LAMP,
    OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_SIGN,OBJ_TREE,OBJ_NONE,OBJ_TREE,
    OBJ_NONE,OBJ_TREE,OBJ_BUSH,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,
    OBJ_TREE,OBJ_NONE,OBJ_LAMP,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_BUSH,
    OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_SIGN,OBJ_NONE,OBJ_TREE,
    OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_BUSH,OBJ_NONE,OBJ_TREE,OBJ_LAMP,
    OBJ_NONE,OBJ_TREE,OBJ_SIGN,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,
};
static const uint8_t s_obj_right[NUM_SEGS] = {
    OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_BUSH,OBJ_TREE,OBJ_NONE,
    OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_LAMP,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,
    OBJ_BUSH,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_SIGN,
    OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_NONE,OBJ_LAMP,OBJ_TREE,OBJ_NONE,OBJ_TREE,
    OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_BUSH,OBJ_TREE,OBJ_NONE,OBJ_TREE,
    OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_SIGN,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_BUSH,
    OBJ_NONE,OBJ_TREE,OBJ_LAMP,OBJ_TREE,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,
    OBJ_TREE,OBJ_SIGN,OBJ_NONE,OBJ_TREE,OBJ_TREE,OBJ_NONE,OBJ_BUSH,OBJ_TREE,
};

/* ================================================================
 * State
 * ================================================================ */

typedef struct {
    int      speed;
    int      steer;        /* lateral car offset, pixels */
    uint32_t cam_z;        /* camera z (world units)     */
    int      cam_seg;      /* current segment index      */
    int      seg_frac;     /* position within segment    */

    int      timer;        /* ticks remaining            */
    int      game_over;
    int      started;

    int      checkpoints;
    uint32_t score;
    uint32_t total_dist;

    int      hill_y;       /* current horizon offset     */
    int      hill_target;

    int      tod;          /* 0=day 1=sunset 2=night     */
    uint32_t tod_dist;
} racer_t;

static racer_t g;

/* per-scanline curve x accumulator saved during road pass
   so object pass can use the same offsets */
#define MAX_ROWS 300
static int row_x_off[MAX_ROWS];  /* x_off[0] = bottom row */

/* ================================================================
 * Helpers
 * ================================================================ */

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

/* Lerp two uint32 colours by t/256 */
static uint32_t col_lerp(uint32_t a, uint32_t b, int t) {
    if (t <= 0)   return a;
    if (t >= 256) return b;
    int r = (int)((a>>16)&0xff) + (((int)((b>>16)&0xff) - (int)((a>>16)&0xff)) * t / 256);
    int gn = (int)((a>> 8)&0xff) + (((int)((b>> 8)&0xff) - (int)((a>> 8)&0xff)) * t / 256);
    int bl = (int)((a    )&0xff) + (((int)((b    )&0xff) - (int)((a    )&0xff)) * t / 256);
    return ((uint32_t)r<<16)|((uint32_t)gn<<8)|(uint32_t)bl;
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

void about_create(int wi) {
    (void)wi;
    g.speed       = 0;
    g.steer       = 0;
    g.cam_z       = 0;
    g.cam_seg     = 0;
    g.seg_frac    = 0;
    g.timer       = START_TICKS;
    g.game_over   = 0;
    g.started     = 0;
    g.checkpoints = 0;
    g.score       = 0;
    g.total_dist  = 0;
    g.hill_y      = 0;
    g.hill_target = 0;
    g.tod         = 0;
    g.tod_dist    = 0;
}

void about_destroy(int wi) { (void)wi; }
void about_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void about_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void about_key(int wi, int kt, char ch) {
    (void)wi; (void)ch;

    if (g.game_over) {
        about_create(wi);
        g.started = 1;
        return;
    }

    switch (kt) {
    case KEY_EVENT_UP:
        g.started = 1;
        if (g.speed < SPEED_MAX) g.speed += 18;
        break;
    case KEY_EVENT_DOWN:
        g.speed -= 25;
        if (g.speed < 0) g.speed = 0;
        break;
    case KEY_EVENT_LEFT:
        g.steer -= 8;
        if (g.steer < -110) g.steer = -110;
        break;
    case KEY_EVENT_RIGHT:
        g.steer += 8;
        if (g.steer > 110) g.steer = 110;
        break;
    default:
        break;
    }
}

void about_tick(int wi) {
    (void)wi;
    if (!g.started || g.game_over) return;

    /* --- Physics --- */
    g.speed -= SPEED_IDLE;
    if (g.speed < 0) g.speed = 0;

    /* Steer re-centres gently */
    if (g.steer > 0) { g.steer -= 2; if (g.steer < 0) g.steer = 0; }
    if (g.steer < 0) { g.steer += 2; if (g.steer > 0) g.steer = 0; }

    /* --- Move forward --- */
    int advance = g.speed / 4;
    g.cam_z      += (uint32_t)advance;
    g.total_dist += (uint32_t)advance;

    /* Update segment */
    g.cam_seg  = (int)((g.cam_z / SEG_LEN) & SEG_MASK);
    g.seg_frac = (int)( g.cam_z % SEG_LEN);

    /* --- Hill tracking --- */
    g.hill_target = s_hill[g.cam_seg] * 10;
    if (g.hill_y < g.hill_target) g.hill_y++;
    if (g.hill_y > g.hill_target) g.hill_y--;

    /* --- Checkpoint detection --- */
    if (g.seg_frac < advance && (g.cam_seg & 15) == 0 && g.cam_seg != 0) {
        g.checkpoints++;
        g.timer += CHECK_TICKS;
        if (g.timer > 9000) g.timer = 9000;
    }

    /* --- Timer --- */
    g.timer--;
    if (g.timer <= 0) {
        g.timer    = 0;
        g.game_over = 1;
        g.score = g.total_dist / 100 + g.checkpoints * 1000u
                + (uint32_t)(g.speed / 2);
    }

    /* --- Time of day --- */
    g.tod_dist += (uint32_t)advance;
    if (g.tod_dist >= TOD_TOTAL)
        g.tod_dist -= TOD_TOTAL;
    if      (g.tod_dist < TOD_DAY_D)   g.tod = 0;
    else if (g.tod_dist < TOD_DAY_D + TOD_SUN_D) g.tod = 1;
    else                                g.tod = 2;
}

/* ================================================================
 * Rendering helpers
 * ================================================================ */

/* Clamp a fill so it doesn't draw outside the window */
static void safe_fill(int x, int y, int w, int h, uint32_t col,
                       int cx, int cy, int cw, int ch) {
    if (w <= 0 || h <= 0) return;
    int x2 = x + w, y2 = y + h;
    if (x  < cx)  x  = cx;
    if (y  < cy)  y  = cy;
    if (x2 > cx+cw) x2 = cx+cw;
    if (y2 > cy+ch) y2 = cy+ch;
    if (x2 <= x || y2 <= y) return;
    kd_fill(x, y, x2-x, y2-y, col);
}

/* TOD-dependent colour sets */
static uint32_t sky_top(int tod) {
    if (tod == 0) return 0x1155AAu;  /* day: blue               */
    if (tod == 1) return 0xCC3300u;  /* sunset: deep orange-red */
    return 0x000818u;                /* night: near-black       */
}
static uint32_t sky_bot(int tod) {
    if (tod == 0) return 0x66AAEEU;  /* day: lighter blue       */
    if (tod == 1) return 0xFF8822u;  /* sunset: orange          */
    return 0x001833u;                /* night: deep blue        */
}
static uint32_t grass_col(int stripe, int tod) {
    if (tod == 0) return stripe ? 0x1A7022u : 0x145A1Au;  /* day green  */
    if (tod == 1) return stripe ? 0x5A4010u : 0x422E0Au;  /* sunset brown */
    return stripe ? 0x0A200Du : 0x061508u;                 /* night dark */
}
static uint32_t road_col(int stripe) {
    return stripe ? 0x484848u : 0x383838u;
}
static uint32_t kerb_col(int stripe) {
    return stripe ? 0xFF3030u : 0xFFFFFFu;
}
static uint32_t tree_leaf(int tod) {
    if (tod == 0) return 0x1E8828u;
    if (tod == 1) return 0x6A5010u;
    return 0x0B3010u;
}
static uint32_t tree_trunk(void)  { return 0x5A2A08u; }
static uint32_t lamp_col(int tod) { return (tod == 2) ? 0xFFEE88u : 0x888888u; }

/* Draw a simple tree sprite centred at (tx, ty), height h */
static void draw_tree(int tx, int ty, int h,
                      int cx, int cy, int cw, int ch, int tod) {
    if (h < 3) return;
    int w = imax(2, h * 2 / 3);
    /* foliage – three stacked layers getting wider */
    uint32_t lc = tree_leaf(tod);
    safe_fill(tx - w/2,       ty - h,     w,     h*4/7, lc, cx,cy,cw,ch);
    safe_fill(tx - w*3/5,     ty - h*3/5, w*6/5, h*3/7, lc, cx,cy,cw,ch);
    safe_fill(tx - w*2/3,     ty - h*2/5, w*4/3, h*3/7, col_lerp(lc,0,60), cx,cy,cw,ch);
    /* trunk */
    int tw = imax(1, w/5);
    safe_fill(tx - tw/2, ty - h*2/5, tw, h*2/5, tree_trunk(), cx,cy,cw,ch);
}

/* Draw a lamp post at (lx, base_y), height h */
static void draw_lamp(int lx, int by, int h,
                      int cx, int cy, int cw, int ch, int tod) {
    if (h < 4) return;
    uint32_t lc = lamp_col(tod);
    safe_fill(lx - 1, by - h,     2,    h,    0x666666u, cx,cy,cw,ch);
    safe_fill(lx - 1, by - h,     8,    1,    0x666666u, cx,cy,cw,ch);
    safe_fill(lx + 5, by - h,     3,    2,    lc,        cx,cy,cw,ch);
}

/* Draw a sign post at (sx, base_y), height h */
static void draw_sign(int sx, int by, int h,
                      int cx, int cy, int cw, int ch) {
    if (h < 4) return;
    int sw = imax(3, h * 3 / 2);
    int sh = imax(2, h * 2 / 3);
    safe_fill(sx - 1, by - h,       2,  h,  0x888888u, cx,cy,cw,ch);
    safe_fill(sx - sw/2, by - h,    sw, sh, 0xEEEE22u, cx,cy,cw,ch);
    safe_fill(sx - sw/2, by - h,    sw, 1,  0x222200u, cx,cy,cw,ch);
    safe_fill(sx - sw/2, by - h + sh - 1, sw, 1, 0x222200u, cx,cy,cw,ch);
}

/* Draw a bush at (bx, by), height h */
static void draw_bush(int bx, int by, int h,
                      int cx, int cy, int cw, int ch, int tod) {
    if (h < 2) return;
    int bw = imax(2, h * 3 / 2);
    uint32_t lc = tod == 0 ? 0x228833u :
                  tod == 1 ? 0x4A3808u : 0x0A1E0Au;
    safe_fill(bx - bw/2, by - h, bw, h, lc, cx,cy,cw,ch);
}

/* Mountains silhouette — draw a series of triangular bumps */
static void draw_mountains(int cx, int cy, int cw, int horizon,
                            uint32_t cam_z, int tod) {
    int band = 20;                          /* mountain band height in px */
    if (band <= 0) return;
    uint32_t mcol = tod == 0 ? 0x335577u :
                    tod == 1 ? 0x553322u : 0x111833u;
    uint32_t mlit = tod == 0 ? 0x446688u :
                    tod == 1 ? 0x664433u : 0x1A2244u;

    /* parallax scroll: mountains move at 1/4 road speed */
    int px = (int)(cam_z >> 3) & (cw - 1);

    for (int mx = -cw; mx < cw*2; mx += 48) {
        int ox = ((mx - px) % cw + cw) % cw - cw/4 + cx;
        int ph = 10 + ((mx * 7 + 33) & 15);  /* height variation 10..25 */
        /* filled triangle via shrinking horizontal lines */
        for (int r = 0; r < ph && r < band; r++) {
            int yw = (ph - r) * (cw/12) / ph;
            if (yw < 1) yw = 1;
            uint32_t c = (r < ph/3) ? mlit : mcol;
            safe_fill(ox - yw, horizon - band + r, yw*2, 1, c,
                      cx, cy-2, cw, band+4);
        }
    }
}

/* Draw the player car */
static void draw_car(int cx, int cy, int cw, int ch) {
    int car_w = 28, car_h = 16;
    int bx = cx + cw/2 - car_w/2 + g.steer;
    int by = cy + ch - 34;

    /* shadow */
    kd_fill(bx + 2, by + car_h + 1, car_w - 4, 2, 0x060606u);

    /* body */
    kd_fill(bx,         by,          car_w,     car_h,   0xEE2222u);
    /* roof highlight */
    kd_fill(bx + 4,     by - 5,      car_w - 8, 6,       0xCC1111u);
    /* windscreen */
    kd_fill(bx + 5,     by + 2,      18,        7,       0x88CCFFu);
    /* windscreen glare */
    kd_fill(bx + 5,     by + 2,      4,         3,       0xCCEEFFu);
    /* body bevel highlight top */
    kd_fill(bx,         by,          car_w,     1,       0xFF6666u);
    /* body bevel shadow bottom */
    kd_fill(bx,         by + car_h - 1, car_w,  1,       0xAA0000u);
    /* wheels */
    kd_fill(bx + 2,     by + car_h,  5,         3,       0x222222u);
    kd_fill(bx + car_w - 7, by + car_h, 5,      3,       0x222222u);
}

/* Draw the HUD overlay */
static void draw_hud(int cx, int cy, int cw, int ch) {
    char buf[24];
    /* top-left: speed */
    kd_str(cx + 6, cy + 6, "KM/H", KA_DIM, 0);
    kd_itoa(g.speed * 2, buf, sizeof(buf));
    kd_str(cx + 6, cy + 14, buf, KA_BRIGHT, 0);

    /* top-right: timer */
    int secs = g.timer / 100;
    int csec = (g.timer % 100) / 10;
    buf[0]  = (char)('0' + secs / 100 % 10);
    buf[1]  = (char)('0' + secs / 10  % 10);
    buf[2]  = (char)('0' + secs % 10);
    buf[3]  = '.';
    buf[4]  = (char)('0' + csec);
    buf[5]  = '\0';
    uint32_t tc = (g.timer < 500) ? KA_RED : KA_YELLOW;
    kd_str(cx + cw - 50, cy + 6, buf, tc, 0);

    /* top-centre: distance */
    kd_utoa(g.total_dist / 100, buf, sizeof(buf));
    kd_str(cx + cw/2 - 24, cy + 6, buf, KA_TEXT, 0);
    kd_str(cx + cw/2 - 24, cy + 14, "m", KA_DIM, 0);

    /* bottom status bar */
    kd_fill(cx, cy + ch - 13, cw, 13, 0x000A04u);
    kd_str(cx + 6, cy + ch - 9,
           "UP:accel  DN:brake  LEFT/RIGHT:steer",
           KA_DIM, 0x000A04u);

    /* checkpoints */
    if (g.checkpoints > 0) {
        kd_str(cx + cw - 100, cy + ch - 9, "CK:", KA_DIM, 0x000A04u);
        kd_itoa(g.checkpoints, buf, sizeof(buf));
        kd_str(cx + cw - 76, cy + ch - 9, buf, KA_YELLOW, 0x000A04u);
    }
}

/* Draw the CHECKPOINT flash */
static void draw_checkpoint_arch(int cx, int cy, int cw,
                                  int road_cx, int road_w) {
    (void)cx; (void)cw;
    /* Gate posts */
    kd_fill(road_cx - road_w - 4, cy, 6, 40, 0xFFCC00u);
    kd_fill(road_cx + road_w - 2, cy, 6, 40, 0xFFCC00u);
    /* Overhead beam */
    kd_fill(road_cx - road_w - 4, cy, road_w*2 + 12, 6, 0xFFCC00u);
    kd_str(road_cx - 32, cy + 10, "CHECKPOINT", KA_BRIGHT, 0xFFCC00u);
}

/* ================================================================
 * Main render
 * ================================================================ */

void about_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;

    /* --- Sky gradient --- */
    {
        int ht = ch * 2 / 5;
        uint32_t ctop = sky_top(g.tod);
        uint32_t cbot = sky_bot(g.tod);
        for (int r = 0; r < ht; r++) {
            uint32_t c = col_lerp(ctop, cbot, r * 256 / (ht + 1));
            kd_fill(cx, cy + r, cw, 1, c);
        }
        /* Night stars */
        if (g.tod == 2) {
            for (int s = 0; s < 24; s++) {
                int sx = cx + (s * 37 + 13) % cw;
                int sy = cy + (s * 29 + 7)  % (ht - 2);
                kd_fill(sx, sy, 1, 1, 0xFFFFDDu);
            }
        }
        /* Sunset sun */
        if (g.tod == 1) {
            int sun_x = cx + cw * 3 / 5;
            int sun_y = cy + ht - 12;
            kd_fill(sun_x - 8, sun_y - 4, 16, 8, 0xFFEE00u);
            kd_fill(sun_x - 6, sun_y - 6, 12, 2, 0xFFDD00u);
        }
    }

    /* --- Effective curve for this frame --- */
    /* Blend near + upcoming segments for smooth curve transition */
    int c0 = s_curve[ g.cam_seg            & SEG_MASK];
    int c1 = s_curve[(g.cam_seg + 1)       & SEG_MASK];
    int c2 = s_curve[(g.cam_seg + 4)       & SEG_MASK];
    int c3 = s_curve[(g.cam_seg + 8)       & SEG_MASK];
    /* weighted blend: heavier on near segments */
    int eff_curve = (c0*4 + c1*3 + c2*2 + c3) / 10;

    /* Horizon y (adjusted for hills) */
    int horizon = cy + ch * 2 / 5 + g.hill_y;
    int road_rows = cy + ch - 13 - horizon;  /* scanlines for road */
    if (road_rows < 1) road_rows = 1;

    /* --- Mountains --- */
    draw_mountains(cx, cy, cw, horizon, g.cam_z, g.tod);

    /* --- Road scanline pass --- */
    /* We render top→bottom (horizon→near).
       x_off accumulates curve: positive curve sweeps road right. */
    int x_off = 0;
    int checkpoint_near = -1;
    int checkpoint_road_cx = cx + cw/2;
    int checkpoint_road_w  = ROAD_HALF;

    for (int row = 0; row < road_rows; row++) {
        int y = horizon + row;
        int p = row + 1;           /* 1 at horizon, increases downward */

        /* Perspective road width */
        int road_w  = (ROAD_HALF * CAM_D) / p;
        int kerb_w  = imax(1, road_w / 10);

        /* Stripe pattern (alternating every 8px, scrolls with cam_z) */
        int stripe = (((uint32_t)row + g.cam_z / 20) / 8) & 1;

        /* Road centre x */
        int rcx = cx + cw/2 + x_off + g.steer * p / (road_rows + 1);

        /* Clamp visible range */
        int left_grass_end = imax(cx, rcx - road_w - kerb_w);
        int right_grass_st = imin(cx + cw, rcx + road_w + kerb_w);

        /* Grass */
        uint32_t gc = grass_col(stripe, g.tod);
        kd_fill(cx, y, left_grass_end - cx, 1, gc);
        kd_fill(right_grass_st, y, cx+cw - right_grass_st, 1, gc);

        /* Kerbs */
        uint32_t kc = kerb_col(stripe);
        safe_fill(rcx - road_w - kerb_w, y, kerb_w, 1, kc, cx,cy,cw,ch);
        safe_fill(rcx + road_w,          y, kerb_w, 1, kc, cx,cy,cw,ch);

        /* Road surface */
        uint32_t rc = road_col(stripe);
        safe_fill(rcx - road_w, y, road_w*2, 1, rc, cx,cy,cw,ch);

        /* Centre dashes */
        if (stripe)
            safe_fill(rcx - 1, y, 2, 1, 0xEEEEEEu, cx,cy,cw,ch);

        /* Save for object pass */
        if (row < MAX_ROWS) row_x_off[row] = x_off;

        /* Checkpoint check: is a checkpoint segment visible here? */
        {
            uint32_t depth_z = g.cam_z + (uint32_t)(p * CAM_D / 2);
            int dseg = (int)((depth_z / SEG_LEN) & SEG_MASK);
            if ((dseg & 15) == 0 && dseg != 0 && row < 12) {
                checkpoint_near   = row;
                checkpoint_road_cx = rcx;
                checkpoint_road_w  = road_w;
            }
        }

        /* Accumulate curve (contributes more as p grows) */
        x_off += (eff_curve * CAM_D) / (p * p + 1);
    }

    /* --- Object pass (far→near so near objects draw on top) --- */
    {
        int max_segs = 18;
        for (int si = max_segs - 1; si >= 0; si--) {
            int seg = (g.cam_seg + si) & SEG_MASK;

            /* Approximate row for this segment */
            int seg_depth_p = 1 + (max_segs - si) * road_rows / max_segs;
            if (seg_depth_p >= road_rows) continue;

            int y_base = horizon + seg_depth_p;
            if (y_base >= cy + ch - 13) continue;

            int road_w = (ROAD_HALF * CAM_D) / (seg_depth_p + 1);
            int ox = (seg_depth_p < MAX_ROWS) ? row_x_off[seg_depth_p] : 0;
            int rcx = cx + cw/2 + ox + g.steer * seg_depth_p / (road_rows + 1);

            /* Object height proportional to perspective */
            int obj_h = imax(2, road_w * 3 / 4);

            /* Left side object */
            int obj_x_l = rcx - road_w - imax(2, road_w/3) - obj_h/2;

            switch (s_obj_left[seg]) {
            case OBJ_TREE:
                draw_tree(obj_x_l, y_base, obj_h, cx,cy,cw,ch, g.tod);
                break;
            case OBJ_SIGN:
                draw_sign(obj_x_l, y_base, obj_h, cx,cy,cw,ch);
                break;
            case OBJ_LAMP:
                draw_lamp(obj_x_l, y_base, obj_h, cx,cy,cw,ch, g.tod);
                break;
            case OBJ_BUSH:
                draw_bush(obj_x_l, y_base, obj_h*2/3, cx,cy,cw,ch, g.tod);
                break;
            default: break;
            }

            /* Right side object */
            int obj_x_r = rcx + road_w + imax(2, road_w/3) + obj_h/2;

            switch (s_obj_right[seg]) {
            case OBJ_TREE:
                draw_tree(obj_x_r, y_base, obj_h, cx,cy,cw,ch, g.tod);
                break;
            case OBJ_SIGN:
                draw_sign(obj_x_r, y_base, obj_h, cx,cy,cw,ch);
                break;
            case OBJ_LAMP:
                draw_lamp(obj_x_r, y_base, obj_h, cx,cy,cw,ch, g.tod);
                break;
            case OBJ_BUSH:
                draw_bush(obj_x_r, y_base, obj_h*2/3, cx,cy,cw,ch, g.tod);
                break;
            default: break;
            }
        }
    }

    /* --- Night headlight cone --- */
    if (g.tod == 2 && g.speed > 0) {
        int car_y = cy + ch - 34;
        int hcx   = cx + cw/2 + g.steer;
        for (int r = 0; r < 60 && car_y - r >= horizon; r++) {
            int hw = 4 + r * 22 / 60;
            int alpha = 50 - r * 45 / 60;
            if (alpha < 5) break;
            uint32_t lc = (uint32_t)((alpha * 0xFFEE88u / 100) & 0xFFFFFFu);
            /* additive-ish blend using a dim fill — approximate */
            safe_fill(hcx - hw, car_y - r, hw*2, 1, lc, cx,cy,cw,ch);
        }
    }

    /* --- Checkpoint arch --- */
    if (checkpoint_near >= 0 && checkpoint_near < 10) {
        draw_checkpoint_arch(cx, horizon, cw,
                             checkpoint_road_cx, checkpoint_road_w);
    }

    /* --- Player car --- */
    draw_car(cx, cy, cw, ch);

    /* --- HUD --- */
    draw_hud(cx, cy, cw, ch);

    /* --- Game Over overlay --- */
    if (g.game_over) {
        /* semi-dark overlay */
        for (int r = 0; r < ch; r += 2)
            kd_fill(cx, cy + r, cw, 1, 0x000000u);

        kd_str(cx + cw/2 - 40, cy + ch/2 - 24, "GAME OVER",  KA_RED,    0);
        kd_str(cx + cw/2 - 40, cy + ch/2 - 10, "SCORE:",     KA_TEXT,   0);
        {
            char sb[16];
            kd_utoa(g.score, sb, sizeof(sb));
            kd_str(cx + cw/2 + 8, cy + ch/2 - 10, sb,        KA_BRIGHT, 0);
        }
        kd_str(cx + cw/2 - 56, cy + ch/2 + 6,
               "Press any key to restart",                     KA_DIM,   0);
    }

    /* --- Title banner if not started --- */
    if (!g.started && !g.game_over) {
        kd_str(cx + cw/2 - 48, cy + ch/2 - 16,
               "SiMPLE RACER",   KA_BRIGHT, 0);
        kd_str(cx + cw/2 - 64, cy + ch/2,
               "Press UP to start",  KA_DIM, 0);
    }
}
