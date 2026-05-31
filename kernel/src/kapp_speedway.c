/* kapp_speedway.c — SiMPLE Speedway: pseudo-3D lap-racing game
 *
 * Rendering follows the same scanline approach as kapp_about.c (SiMPLE Racer):
 *   - Row 0 = nearest road (top of road area, just below horizon line)
 *   - Row N = farthest visible road (bottom of road area)
 *   - road_w at row = ROAD_HALF * CAM_D / (row+1)    (perspective scale)
 *   - depth at row ≈ cam_z + (row+1) * CAM_D / 2     (world-unit depth)
 *
 * Extensions over SiMPLE Racer:
 *   - 3-lap race with lap counter and best-lap timer
 *   - 3 AI opponents with per-car speed and overtaking behaviour
 *   - Race position tracking
 *   - 3-2-1-GO countdown start sequence
 *   - Race-complete finish screen with position / time
 *   - Collision detection (speed penalty when side-by-side)
 *   - ESC closes the window via wm_close_kapp()
 */

#include "kapp.h"
#include "pit.h"
#include "string.h"
#include "wm.h"

/* ================================================================
 * Constants
 * ================================================================ */

#define TRACK_SEGS    96          /* segments per lap (must be power-of-2) */
#define TRACK_MASK    95
#define SEG_LEN       200         /* world units per segment               */
#define TRACK_LEN     (TRACK_SEGS * SEG_LEN)   /* = 19 200 wu / lap       */

#define CAM_D         160         /* camera projection depth               */
#define ROAD_HALF     85          /* half road width (world units)         */

#define SPEED_MAX     360         /* max player speed (raw)                */
#define SPEED_IDLE    3           /* auto-decel per tick                   */
#define ACCEL         16          /* speed gained per key-press            */
#define BRAKE_RATE    28          /* speed lost per key-press              */
#define STEER_RATE    12          /* steer change per key-press (pixels)   */
#define STEER_MAX     120         /* maximum steer offset in pixels        */
#define STEER_RETURN  2           /* steer re-centre rate per tick         */
#define OFFROAD_DECEL 8           /* extra decel when off road             */
#define OFFROAD_PUSH  5           /* pixels pushed back per tick off-road  */

#define LAPS_TOTAL    3           /* race laps                             */
#define COUNTDOWN_DUR 300         /* 3 s at 100 Hz                         */
#define NUM_AI        3           /* AI opponent count                     */
#define DASH_H        64          /* dashboard strip height (pixels)       */

/* Roadside object type IDs */
#define OBJ_NONE  0
#define OBJ_TREE  1
#define OBJ_SIGN  2
#define OBJ_LAMP  3
#define OBJ_BUSH  4

/* Max scanlines stored for the object-pass x-offset table */
#define MAX_SCAN  320

/* ================================================================
 * Track data
 * ================================================================ */

/* Curve: -8..+8; negative = left, positive = right */
static const signed char s_curve[TRACK_SEGS] = {
    0, 0, 0, 0, 0, 0, 0, 0,   /* 00-07  start straight              */
   -2,-4,-6,-7,-6,-4,-2, 0,   /* 08-15  long left sweep             */
    0, 0, 0, 0, 0, 0, 0, 0,   /* 16-23  straight                    */
    3, 5, 7, 8, 7, 5, 3, 1,   /* 24-31  sweeping right              */
    0, 0, 0, 0, 0, 0, 0, 0,   /* 32-39  straight                    */
   -3,-6,-8,-6,-3, 0, 0, 0,   /* 40-47  sharp left                  */
    0, 0, 0, 0, 0, 0, 0, 0,   /* 48-55  straight                    */
    4, 7, 8, 7, 4, 2, 0, 0,   /* 56-63  right chicane entry         */
   -2,-5,-7,-5,-2, 0, 0, 0,   /* 64-71  left chicane exit           */
    0, 0, 0, 0, 0, 0, 0, 0,   /* 72-79  back straight               */
    3, 6, 7, 6, 3, 0, 0, 0,   /* 80-87  final right                 */
   -2,-4,-6,-4,-2, 0, 0, 0,   /* 88-95  home stretch                */
};

static const uint8_t s_obj_left[TRACK_SEGS] = {
    OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_LAMP, OBJ_NONE, OBJ_TREE, OBJ_BUSH, OBJ_TREE,
    OBJ_NONE, OBJ_TREE, OBJ_SIGN, OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_LAMP,
    OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_TREE, OBJ_NONE,
    OBJ_TREE, OBJ_SIGN, OBJ_TREE, OBJ_NONE, OBJ_LAMP, OBJ_TREE, OBJ_NONE, OBJ_TREE,
    OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_BUSH, OBJ_NONE, OBJ_TREE, OBJ_SIGN, OBJ_TREE,
    OBJ_NONE, OBJ_TREE, OBJ_LAMP, OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_BUSH,
    OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_SIGN, OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_LAMP,
    OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_TREE, OBJ_NONE, OBJ_TREE,
    OBJ_TREE, OBJ_SIGN, OBJ_NONE, OBJ_TREE, OBJ_LAMP, OBJ_TREE, OBJ_NONE, OBJ_TREE,
    OBJ_BUSH, OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_SIGN, OBJ_NONE, OBJ_TREE, OBJ_LAMP,
    OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_BUSH, OBJ_NONE, OBJ_TREE, OBJ_SIGN, OBJ_TREE,
    OBJ_LAMP, OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_TREE, OBJ_NONE,
};

static const uint8_t s_obj_right[TRACK_SEGS] = {
    OBJ_NONE, OBJ_TREE, OBJ_BUSH, OBJ_TREE, OBJ_LAMP, OBJ_NONE, OBJ_TREE, OBJ_SIGN,
    OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_LAMP, OBJ_NONE, OBJ_BUSH, OBJ_TREE, OBJ_NONE,
    OBJ_SIGN, OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_LAMP, OBJ_TREE, OBJ_NONE, OBJ_BUSH,
    OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_SIGN, OBJ_TREE, OBJ_NONE, OBJ_LAMP, OBJ_NONE,
    OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_TREE, OBJ_TREE, OBJ_SIGN, OBJ_NONE, OBJ_LAMP,
    OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_BUSH, OBJ_NONE, OBJ_SIGN, OBJ_TREE, OBJ_NONE,
    OBJ_LAMP, OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_BUSH, OBJ_TREE, OBJ_SIGN, OBJ_NONE,
    OBJ_TREE, OBJ_LAMP, OBJ_NONE, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_SIGN,
    OBJ_NONE, OBJ_TREE, OBJ_LAMP, OBJ_TREE, OBJ_NONE, OBJ_SIGN, OBJ_TREE, OBJ_BUSH,
    OBJ_TREE, OBJ_LAMP, OBJ_NONE, OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_TREE, OBJ_SIGN,
    OBJ_TREE, OBJ_NONE, OBJ_LAMP, OBJ_TREE, OBJ_TREE, OBJ_NONE, OBJ_BUSH, OBJ_TREE,
    OBJ_NONE, OBJ_SIGN, OBJ_TREE, OBJ_LAMP, OBJ_TREE, OBJ_NONE, OBJ_TREE, OBJ_NONE,
};

/* ================================================================
 * Types
 * ================================================================ */

typedef struct {
    int cam_z;      /* world position along track (0 .. TRACK_LEN-1)  */
    int cam_x;      /* lateral pixel offset from road centre           */
    int speed;      /* raw speed (advance per tick = speed/16)         */
    int lap;        /* current lap number (1-based, 0 before start)    */
    int finished;   /* 1 once the car completes LAPS_TOTAL laps        */
    uint32_t seed;  /* per-car LCG seed for speed jitter               */
} ai_car_t;

typedef enum {
    PHASE_COUNTDOWN = 0,
    PHASE_RACING,
    PHASE_FINISHED
} sw_phase_t;

typedef struct {
    int        cam_z;       /* player world position                   */
    int        steer;       /* lateral pixel offset (+ = right)        */
    int        speed;       /* raw speed                               */
    int        lap;         /* 1-based, 0 before countdown ends        */
    int        position;    /* race position (1 = leading)             */

    sw_phase_t phase;
    int        countdown;   /* ticks left in countdown                 */
    int        race_ticks;  /* ticks since race start                  */
    int        lap_start;   /* race_ticks at start of current lap      */
    int        best_lap;    /* fastest lap (ticks); 0 = none           */
    int        finish_pos;  /* position when race ended                */

    ai_car_t   ai[NUM_AI];
    uint32_t   last_tick;
} speedway_t;

static speedway_t sw;

/* Saved per-row x-offset for the two-pass (road + objects) renderer */
static int row_x_off[MAX_SCAN];

/* ================================================================
 * Helpers
 * ================================================================ */

static int sw_imax(int a, int b) { return a > b ? a : b; }
static int sw_imin(int a, int b) { return a < b ? a : b; }
static int sw_iabs(int v)        { return v < 0 ? -v : v; }

static void sw_safe_fill(int x, int y, int w, int h, uint32_t col,
                          int cx, int cy, int cw, int ch) {
    int x2 = x + w, y2 = y + h;
    if (x  < cx)     x  = cx;
    if (y  < cy)     y  = cy;
    if (x2 > cx+cw)  x2 = cx+cw;
    if (y2 > cy+ch)  y2 = cy+ch;
    if (x2 > x && y2 > y) kd_fill(x, y, x2-x, y2-y, col);
}

static uint32_t sw_rand(uint32_t *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed >> 16;
}

/* Format ticks as "M:SS.cc" into buf (needs >=8 bytes) */
static void ticks_to_str(int ticks, char *buf, int cap) {
    if (cap < 8) { if (cap > 0) buf[0] = '\0'; return; }
    int secs = ticks / 100;
    int frac = ticks % 100;
    int mins = secs  / 60;
    secs     = secs  % 60;
    buf[0] = (char)('0' + mins);
    buf[1] = ':';
    buf[2] = (char)('0' + secs / 10);
    buf[3] = (char)('0' + secs % 10);
    buf[4] = '.';
    buf[5] = (char)('0' + frac / 10);
    buf[6] = (char)('0' + frac % 10);
    buf[7] = '\0';
}

/* ================================================================
 * Race reset
 * ================================================================ */

static void sw_init_race(void) {
    sw.cam_z      = 0;
    sw.steer      = 0;
    sw.speed      = 0;
    sw.lap        = 0;
    sw.position   = 1 + NUM_AI;
    sw.phase      = PHASE_COUNTDOWN;
    sw.countdown  = COUNTDOWN_DUR;
    sw.race_ticks = 0;
    sw.lap_start  = 0;
    sw.best_lap   = 0;
    sw.finish_pos = 0;

    /* Stagger AI starts behind the player on the grid */
    static const int ai_base_speed[NUM_AI] = { 225, 210, 250 };
    static const int ai_start_x[NUM_AI]    = { -20, 20, 0 };

    for (int i = 0; i < NUM_AI; i++) {
        sw.ai[i].cam_z    = -(i + 1) * (SEG_LEN / 2);
        sw.ai[i].cam_x    = ai_start_x[i];
        sw.ai[i].speed    = ai_base_speed[i];
        sw.ai[i].lap      = 0;
        sw.ai[i].finished = 0;
        sw.ai[i].seed     = 0xBEEF0000u ^ (uint32_t)(i * 0x5A5A + 7);
    }
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

void speedway_create(int wi)  { (void)wi; sw.last_tick = pit_ticks(); sw_init_race(); }
void speedway_destroy(int wi) { (void)wi; }

void speedway_click(int wi, int x, int y) {
    (void)x; (void)y;
    if (sw.phase == PHASE_FINISHED) { sw_init_race(); (void)wi; }
}

void speedway_mouse(int wi, int x, int y, int btn) {
    (void)wi; (void)x; (void)y; (void)btn;
}

/* ================================================================
 * Input
 * ================================================================ */

void speedway_key(int wi, int kt, char ch) {
    /* ESC → close this window and return to desktop */
    if (kt == KEY_EVENT_CHAR && ch == 27) {
        wm_close_kapp(wi);
        return;
    }

    /* After the race: R or Space restarts */
    if (sw.phase == PHASE_FINISHED) {
        if (kt == KEY_EVENT_CHAR && (ch == 'r' || ch == 'R' || ch == ' '))
            sw_init_race();
        return;
    }

    /* Arrow keys only valid once racing */
    if (sw.phase != PHASE_RACING) return;

    switch (kt) {
    case KEY_EVENT_UP:
        sw.speed += ACCEL;
        if (sw.speed > SPEED_MAX) sw.speed = SPEED_MAX;
        break;
    case KEY_EVENT_DOWN:
        sw.speed -= BRAKE_RATE;
        if (sw.speed < 0) sw.speed = 0;
        break;
    case KEY_EVENT_LEFT:
        sw.steer -= STEER_RATE;
        if (sw.steer < -STEER_MAX) sw.steer = -STEER_MAX;
        break;
    case KEY_EVENT_RIGHT:
        sw.steer += STEER_RATE;
        if (sw.steer > STEER_MAX) sw.steer = STEER_MAX;
        break;
    default: break;
    }
}

/* ================================================================
 * Tick — physics + AI + race logic
 * ================================================================ */

void speedway_tick(int wi) {
    (void)wi;
    uint32_t now = pit_ticks();
    if (now == sw.last_tick) return;
    sw.last_tick = now;

    /* --- Countdown phase --- */
    if (sw.phase == PHASE_COUNTDOWN) {
        sw.countdown--;
        if (sw.countdown <= 0) {
            sw.phase = PHASE_RACING;
            sw.lap   = 1;
            /* Bring AI to start line */
            for (int i = 0; i < NUM_AI; i++) {
                if (sw.ai[i].cam_z < 0) sw.ai[i].cam_z = 0;
                sw.ai[i].lap = 1;
            }
        }
        return;
    }

    if (sw.phase == PHASE_FINISHED) return;

    /* --- Racing phase --- */
    sw.race_ticks++;

    /* Player physics */
    sw.speed -= SPEED_IDLE;
    if (sw.speed < 0) sw.speed = 0;

    /* Steer re-centre */
    if (sw.steer > 0) { sw.steer -= STEER_RETURN; if (sw.steer < 0) sw.steer = 0; }
    if (sw.steer < 0) { sw.steer += STEER_RETURN; if (sw.steer > 0) sw.steer = 0; }

    /* Off-road: steer beyond ~ROAD_HALF pixel equivalent */
    if (sw_iabs(sw.steer) > ROAD_HALF) {
        sw.speed -= OFFROAD_DECEL;
        if (sw.speed < 0) sw.speed = 0;
        if (sw.steer >  ROAD_HALF) { sw.steer -= OFFROAD_PUSH; if (sw.steer < ROAD_HALF)  sw.steer = ROAD_HALF; }
        if (sw.steer < -ROAD_HALF) { sw.steer += OFFROAD_PUSH; if (sw.steer > -ROAD_HALF) sw.steer = -ROAD_HALF; }
    }

    /* Advance player */
    int advance = sw.speed / 16;
    sw.cam_z += advance;

    /* Lap detection */
    if (sw.cam_z >= TRACK_LEN) {
        sw.cam_z -= TRACK_LEN;
        sw.lap++;
        int lap_time = sw.race_ticks - sw.lap_start;
        sw.lap_start = sw.race_ticks;
        if (sw.best_lap == 0 || lap_time < sw.best_lap)
            sw.best_lap = lap_time;

        if (sw.lap > LAPS_TOTAL) {
            sw.phase      = PHASE_FINISHED;
            sw.finish_pos = sw.position;
            return;
        }
    }

    /* --- AI updates --- */
    for (int i = 0; i < NUM_AI; i++) {
        ai_car_t *ai = &sw.ai[i];
        if (ai->finished) continue;

        /* Speed jitter: ±8 raw units per tick */
        int jitter = (int)(sw_rand(&ai->seed) & 15u) - 8;
        int spd = ai->speed + jitter;
        if (spd < 60) spd = 60;

        /* Gentle re-centre */
        if      (ai->cam_x >  2) ai->cam_x -= 2;
        else if (ai->cam_x < -2) ai->cam_x += 2;
        else                      ai->cam_x  = 0;

        /* Advance */
        ai->cam_z += spd / 16;
        if (ai->cam_z < 0) ai->cam_z += TRACK_LEN;

        /* Lap wrap */
        if (ai->cam_z >= TRACK_LEN) {
            ai->cam_z -= TRACK_LEN;
            ai->lap++;
            if (ai->lap > LAPS_TOTAL) ai->finished = 1;
        }

        /* Simple collision with player: same-lap or one-lap-apart check */
        int lap_diff = sw.lap - ai->lap;
        if (lap_diff >= -1 && lap_diff <= 1) {
            int dz = ai->cam_z - sw.cam_z + lap_diff * TRACK_LEN;
            int dx = sw_iabs(ai->cam_x - sw.steer);
            if (sw_iabs(dz) < SEG_LEN / 2 && dx < 55) {
                /* Speed penalty for both */
                sw.speed  = sw.speed  * 3 / 4;
                ai->speed = ai->speed * 3 / 4;
                if (ai->speed < 60) ai->speed = 60;
            }
        }
    }

    /* --- Position: count how many AI are further along in the race --- */
    int player_total = sw.cam_z + sw.lap * TRACK_LEN;
    int pos = 1;
    for (int i = 0; i < NUM_AI; i++) {
        int ai_total = sw.ai[i].cam_z + sw.ai[i].lap * TRACK_LEN;
        if (ai_total > player_total) pos++;
    }
    sw.position = pos;
}

/* ================================================================
 * Rendering helpers
 * ================================================================ */

static void draw_tree_sw(int tx, int ty, int h,
                          int cx, int cy, int cw, int ch) {
    if (h < 3) return;
    int w = sw_imax(2, h * 2 / 3);
    sw_safe_fill(tx - w/2,   ty - h,       w,     h * 4/7, 0x1E8828u, cx,cy,cw,ch);
    sw_safe_fill(tx - w*3/5, ty - h*3/5,   w*6/5, h * 3/7, 0x1E8828u, cx,cy,cw,ch);
    sw_safe_fill(tx - w*2/3, ty - h*2/5,   w*4/3, h * 3/7, 0x156020u, cx,cy,cw,ch);
    sw_safe_fill(tx - sw_imax(1,w/5)/2, ty - h*2/5,
                 sw_imax(1,w/5), h*2/5,            0x5A2A08u, cx,cy,cw,ch);
}

static void draw_bush_sw(int bx, int by, int h,
                          int cx, int cy, int cw, int ch) {
    if (h < 2) return;
    int bw = sw_imax(2, h * 3 / 2);
    sw_safe_fill(bx - bw/2, by - h, bw, h, 0x228833u, cx,cy,cw,ch);
}

static void draw_sign_sw(int sx, int by, int h,
                          int cx, int cy, int cw, int ch) {
    if (h < 4) return;
    int sw2 = sw_imax(3, h * 3 / 2);
    int sh  = sw_imax(2, h * 2 / 3);
    sw_safe_fill(sx - 1,     by - h,  2,   h,  0x888888u, cx,cy,cw,ch);
    sw_safe_fill(sx - sw2/2, by - h,  sw2, sh, 0xEEEE22u, cx,cy,cw,ch);
    sw_safe_fill(sx - sw2/2, by - h,  sw2, 1,  0x222200u, cx,cy,cw,ch);
}

static void draw_lamp_sw(int lx, int by, int h,
                          int cx, int cy, int cw, int ch) {
    if (h < 4) return;
    sw_safe_fill(lx - 1, by - h, 2, h, 0x666666u, cx,cy,cw,ch);
    sw_safe_fill(lx - 1, by - h, 8, 1, 0x666666u, cx,cy,cw,ch);
    sw_safe_fill(lx + 5, by - h, 3, 2, 0xAAAA44u, cx,cy,cw,ch);
}

/* Draw an AI car sprite: bottom-centre at (scr_x, scr_y), scaled by road_w */
static void draw_ai_sprite(int scr_x, int scr_y, int road_w,
                            int cx, int cy, int cw, int clip_bot,
                            uint32_t body_col) {
    /* Scale car from world-unit size via road_w / ROAD_HALF ratio */
    int car_w = sw_imax(4, 50 * road_w / ROAD_HALF);
    int car_h = sw_imax(3, 26 * road_w / ROAD_HALF);

    int x0 = scr_x - car_w / 2;
    int y0 = scr_y - car_h;
    int ch2 = clip_bot - cy;

    /* Body */
    sw_safe_fill(x0, y0, car_w, car_h, body_col, cx, cy, cw, ch2);
    /* Windshield */
    int ws_h = sw_imax(1, car_h / 3);
    sw_safe_fill(x0 + car_w/5, y0, car_w * 3/5, ws_h, 0x88CCEEu, cx, cy, cw, ch2);
    /* Wheels */
    int ww = sw_imax(1, car_w / 5);
    int wh = sw_imax(1, car_h / 5);
    sw_safe_fill(x0,              scr_y - wh, ww, wh, 0x111111u, cx, cy, cw, ch2);
    sw_safe_fill(x0 + car_w - ww, scr_y - wh, ww, wh, 0x111111u, cx, cy, cw, ch2);
}

/* Draw the player's car at the bottom of the road area */
static void draw_player_car(int cx, int cw, int road_bot) {
    int car_w = 36, car_h = 20;
    int bx = cx + cw/2 - car_w/2 + sw.steer;
    int by = road_bot - 4;

    /* Shadow */
    kd_fill(bx + 3, by + 2, car_w - 6, 2, 0x060606u);
    /* Body */
    kd_fill(bx,          by - car_h, car_w,     car_h, 0x2244BBu);
    /* Roof */
    kd_fill(bx + 5,      by - car_h - 7, car_w-10, 7, 0x1A3399u);
    /* Windscreen */
    kd_fill(bx + 6,      by - car_h + 2, car_w-12, 8, 0x88CCFFu);
    kd_fill(bx + 6,      by - car_h + 2, 3,         4, 0xCCEEFFu);
    /* Highlight / shadow edge */
    kd_fill(bx,          by - car_h,     car_w, 1, 0x4466DDu);
    kd_fill(bx,          by - 1,         car_w, 1, 0x112266u);
    /* Wheels */
    kd_fill(bx + 3,      by, 6, 4, 0x222222u);
    kd_fill(bx+car_w-9,  by, 6, 4, 0x222222u);
}

/* Colour-lerp two 24-bit RGB values by t/256 */
static uint32_t sw_lerp(uint32_t a, uint32_t b, int t) {
    if (t <= 0)   return a;
    if (t >= 256) return b;
    int r  = (int)((a>>16)&0xffu) + ((int)((b>>16)&0xffu) - (int)((a>>16)&0xffu)) * t / 256;
    int gn = (int)((a>> 8)&0xffu) + ((int)((b>> 8)&0xffu) - (int)((a>> 8)&0xffu)) * t / 256;
    int bl = (int)((a    )&0xffu) + ((int)((b    )&0xffu) - (int)((a    )&0xffu)) * t / 256;
    return ((uint32_t)r << 16) | ((uint32_t)gn << 8) | (uint32_t)bl;
}

/* ================================================================
 * Main render
 * ================================================================ */

void speedway_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;

    int horizon  = cy + ch * 2 / 5;         /* horizon Y on screen             */
    int road_bot = cy + ch - DASH_H;        /* bottom of road, top of dash     */
    int road_rows = road_bot - horizon;
    if (road_rows < 1) road_rows = 1;

    /* ---- Sky gradient ---- */
    {
        int ht = horizon - cy;
        for (int r = 0; r < ht; r++) {
            int t = r * 256 / (ht + 1);
            kd_fill(cx, cy + r, cw, 1, sw_lerp(0x001033u, 0x2266CCu, t));
        }
    }

    /* ---- Distant mountain silhouette (parallax-scrolling) ---- */
    {
        int band = sw_imin(20, horizon - cy - 8);
        if (band > 0) {
            int px = (int)((uint32_t)sw.cam_z >> 4) & (cw - 1);
            for (int mx = -cw; mx < cw * 2; mx += 56) {
                int ox = ((mx - px) % cw + cw) % cw - cw/4 + cx;
                int ph = 10 + ((mx * 7 + 33) & 15);
                for (int rr = 0; rr < ph && rr < band; rr++) {
                    int yw = (ph - rr) * (cw/10) / ph;
                    if (yw < 1) yw = 1;
                    uint32_t mc = (rr < ph/3) ? 0x446688u : 0x335577u;
                    sw_safe_fill(ox - yw, horizon - band + rr, yw * 2, 1, mc,
                                 cx, cy, cw, horizon - cy + 4);
                }
            }
        }
    }

    /* ---- Effective curve (blend near segments for smoothness) ---- */
    int player_seg = (sw.cam_z / SEG_LEN) & TRACK_MASK;
    {
        int c0 = s_curve[player_seg];
        int c1 = s_curve[(player_seg + 1) & TRACK_MASK];
        int c2 = s_curve[(player_seg + 4) & TRACK_MASK];
        /* Weighted average — heavier on near segment */
        int eff_curve = (c0*4 + c1*3 + c2*2) / 9;

        /* ---- Road scanline pass (row 0 = top/near, row N = bottom/far) ---- */
        int x_off = 0;
        int gate_row = -1, gate_rcx = cx + cw/2, gate_rw = ROAD_HALF;

        for (int row = 0; row < road_rows && row < MAX_SCAN; row++) {
            int y = horizon + row;
            int p = row + 1;

            int road_w = (ROAD_HALF * CAM_D) / p;
            int kerb_w = sw_imax(1, road_w / 8);

            /* Stripe: alternates every 8 rows, scrolls with camera */
            int stripe = (((uint32_t)row + (uint32_t)sw.cam_z / 20u) / 8) & 1;

            /* Road centre x, shifted right when player steers left */
            int rcx = cx + cw/2 + x_off + sw.steer * p / (road_rows + 1);

            /* Save x-offset for two-pass rendering */
            row_x_off[row] = x_off + sw.steer * p / (road_rows + 1);

            /* Left grass */
            {
                int le = sw_imax(cx, rcx - road_w - kerb_w);
                if (le > cx) kd_fill(cx, y, le - cx, 1, stripe ? 0x1A7022u : 0x145A1Au);
            }
            /* Right grass */
            {
                int rs = sw_imin(cx + cw, rcx + road_w + kerb_w);
                if (rs < cx + cw) kd_fill(rs, y, cx + cw - rs, 1, stripe ? 0x1A7022u : 0x145A1Au);
            }

            /* Kerb (red/white rumble strips) */
            {
                uint32_t kc = stripe ? 0xFF3030u : 0xFFFFFFu;
                sw_safe_fill(rcx - road_w - kerb_w, y, kerb_w, 1, kc, cx,cy,cw,ch);
                sw_safe_fill(rcx + road_w,           y, kerb_w, 1, kc, cx,cy,cw,ch);
            }

            /* Road surface */
            sw_safe_fill(rcx - road_w, y, road_w * 2, 1,
                         stripe ? 0x484848u : 0x383838u, cx,cy,cw,ch);

            /* Centre dashed line */
            if (stripe)
                sw_safe_fill(rcx - 1, y, 2, 1, 0xEEEE22u, cx,cy,cw,ch);

            /* Identify start/finish gate and roadside poles */
            {
                uint32_t depth_z = (uint32_t)sw.cam_z + (uint32_t)(p * CAM_D / 2);
                int dseg = (int)((depth_z / (uint32_t)SEG_LEN) & (uint32_t)TRACK_MASK);

                /* Roadside marker poles every 4 segments */
                if (dseg % 4 == 0) {
                    int ph = sw_imax(2, road_w * 2 / 3);
                    int pw = sw_imax(1, road_w / 18);
                    uint32_t pc = stripe ? 0xFF4444u : 0xCCCCCCu;
                    sw_safe_fill(rcx - road_w - kerb_w - pw*3, y - ph, pw, ph, pc, cx,cy,cw,ch);
                    sw_safe_fill(rcx + road_w + kerb_w + pw*2, y - ph, pw, ph, pc, cx,cy,cw,ch);
                }

                /* Start/finish gate */
                if (dseg == 0 && row < 30) {
                    gate_row = row;
                    gate_rcx = rcx;
                    gate_rw  = road_w;
                }
            }

            /* Accumulate curve offset for next row */
            x_off += (eff_curve * CAM_D) / (p * p + 1);
        }

        /* ---- Start/Finish arch ---- */
        if (gate_row >= 0 && gate_row < 20) {
            int gy  = horizon + gate_row;
            int gh  = sw_imax(4, gate_rw * 3 / 2);
            uint32_t gc = 0xFFCC00u;
            sw_safe_fill(gate_rcx - gate_rw - 8, gy - gh, 7, gh + 2, gc, cx,cy,cw,ch);
            sw_safe_fill(gate_rcx + gate_rw + 1,  gy - gh, 7, gh + 2, gc, cx,cy,cw,ch);
            sw_safe_fill(gate_rcx - gate_rw - 8, gy - gh, gate_rw*2+16, 5, gc, cx,cy,cw,ch);
            if (gate_row < 10)
                kd_str(gate_rcx - 40, gy - gh + 5, "START/FINISH", KA_BRIGHT, gc);
        }

        /* ---- Roadside object pass (far→near so near draws on top) ---- */
        {
            int vis = sw_imin(20, road_rows * 3 / 4);
            for (int si = vis - 1; si >= 0; si--) {
                int seg    = (player_seg + si + 1) & TRACK_MASK;
                int seg_p  = 1 + (vis - si - 1) * road_rows / vis;
                if (seg_p >= road_rows) continue;

                int y_base = horizon + seg_p;
                if (y_base >= road_bot) continue;

                int road_w = (ROAD_HALF * CAM_D) / (seg_p + 1);
                int xo     = (seg_p < MAX_SCAN) ? row_x_off[seg_p] : 0;
                int rcx2   = cx + cw/2 + xo;

                int obj_h  = sw_imax(2, road_w * 3 / 4);
                int margin = sw_imax(2, road_w / 3);
                int oxl    = rcx2 - road_w - margin - obj_h/2;
                int oxr    = rcx2 + road_w + margin + obj_h/2;

                switch (s_obj_left[seg]) {
                case OBJ_TREE: draw_tree_sw(oxl, y_base, obj_h, cx,cy,cw,ch); break;
                case OBJ_BUSH: draw_bush_sw(oxl, y_base, obj_h*2/3, cx,cy,cw,ch); break;
                case OBJ_SIGN: draw_sign_sw(oxl, y_base, obj_h, cx,cy,cw,ch); break;
                case OBJ_LAMP: draw_lamp_sw(oxl, y_base, obj_h, cx,cy,cw,ch); break;
                default: break;
                }
                switch (s_obj_right[seg]) {
                case OBJ_TREE: draw_tree_sw(oxr, y_base, obj_h, cx,cy,cw,ch); break;
                case OBJ_BUSH: draw_bush_sw(oxr, y_base, obj_h*2/3, cx,cy,cw,ch); break;
                case OBJ_SIGN: draw_sign_sw(oxr, y_base, obj_h, cx,cy,cw,ch); break;
                case OBJ_LAMP: draw_lamp_sw(oxr, y_base, obj_h, cx,cy,cw,ch); break;
                default: break;
                }
            }
        }
    } /* end eff_curve scope */

    /* ---- AI car sprites ---- */
    static const uint32_t ai_cols[NUM_AI]  = { 0xFF4444u, 0x44CCFFu, 0xFFCC22u };
    static const char    *ai_names[NUM_AI] = { "RED", "BLU", "YEL" };

    for (int i = 0; i < NUM_AI; i++) {
        ai_car_t *ai = &sw.ai[i];

        /* Relative Z: positive means AI is ahead of player */
        int lap_off = (ai->lap - sw.lap) * TRACK_LEN;
        int rel_z   = ai->cam_z - sw.cam_z + lap_off;

        if (rel_z <= 0 || rel_z > road_rows * CAM_D / 2) continue;

        /* Map depth to screen row using kapp_about depth formula:
         * depth = cam_z + p * CAM_D / 2  →  p = rel_z*2/CAM_D, row = p-1 */
        int p_ai  = rel_z * 2 / CAM_D;
        if (p_ai < 1) p_ai = 1;
        int row_ai = p_ai - 1;
        if (row_ai >= road_rows) continue;

        int y_car  = horizon + row_ai;
        int road_w = (ROAD_HALF * CAM_D) / p_ai;
        int xo     = (row_ai < MAX_SCAN) ? row_x_off[row_ai] : 0;
        int rcx2   = cx + cw/2 + xo;

        /* AI lateral position on screen */
        int ai_x_px  = rcx2 + (ai->cam_x - sw.steer) * road_w / ROAD_HALF;
        if (ai_x_px < cx - 40 || ai_x_px > cx + cw + 40) continue;

        draw_ai_sprite(ai_x_px, y_car, road_w,
                       cx, cy, cw, road_bot, ai_cols[i]);

        /* Car label when large enough to read */
        if (road_w > 22 && row_ai > road_rows / 4) {
            int label_h = sw_imax(1, 26 * road_w / ROAD_HALF);
            int lab_y   = y_car - label_h - 8;
            if (lab_y > horizon && lab_y < road_bot - 8)
                kd_str(ai_x_px - 12, lab_y, ai_names[i], ai_cols[i], 0x383838u);
        }
    }

    /* ---- Player car ---- */
    draw_player_car(cx, cw, road_bot);

    /* ---- Dashboard strip ---- */
    kd_fill(cx, road_bot, cw, DASH_H, 0x0D0D0Du);
    kd_fill(cx, road_bot, cw, 2, 0x33AA55u);

    /* Speed gauge */
    {
        char buf[16];
        int spd_kmh = sw.speed * 55 / 100;
        kd_str(cx + 8,  road_bot + 8,  "SPD",  KA_DIM,    0x0D0D0Du);
        kd_itoa(spd_kmh, buf, 16);
        kd_str(cx + 36, road_bot + 8,  buf,    KA_BRIGHT,  0x0D0D0Du);
        kd_str(cx + 60, road_bot + 8,  "km/h", KA_DIM,    0x0D0D0Du);
        /* Speed bar */
        int bw  = 104;
        int bf  = sw_imin(bw, spd_kmh * bw / 360);
        kd_fill(cx + 8,          road_bot + 22, bw, 7, 0x1A2A1Au);
        kd_fill(cx + 8,          road_bot + 22, bf, 7, 0x44FF88u);
        kd_rect(cx + 8,          road_bot + 22, bw, 7, 0x2A4A2Au);
    }

    /* Gear indicator */
    {
        int gear = sw_imin(6, sw.speed * 6 / SPEED_MAX + 1);
        char gbuf[2] = { (char)('0' + gear), '\0' };
        kd_str(cx + 124, road_bot + 8,  "GEAR", KA_DIM,    0x0D0D0Du);
        kd_str(cx + 158, road_bot + 8,  gbuf,   KA_YELLOW,  0x0D0D0Du);
    }

    /* Lap counter (centred) */
    {
        int show_lap = sw_imin(LAPS_TOTAL, sw_imax(1, sw.lap));
        char buf[8];
        kd_str(cx + cw/2 - 66, road_bot + 8, "LAP",    KA_DIM,    0x0D0D0Du);
        kd_itoa(show_lap, buf, 8);
        kd_str(cx + cw/2 - 36, road_bot + 8, buf,      KA_BRIGHT,  0x0D0D0Du);
        kd_str(cx + cw/2 - 24, road_bot + 8, "/",      KA_DIM,    0x0D0D0Du);
        kd_itoa(LAPS_TOTAL, buf, 8);
        kd_str(cx + cw/2 - 14, road_bot + 8, buf,      KA_DIM,    0x0D0D0Du);
    }

    /* Race position */
    {
        char pbuf[8];
        kd_str(cx + cw/2 - 66, road_bot + 24, "POS",   KA_DIM,    0x0D0D0Du);
        kd_str(cx + cw/2 - 36, road_bot + 24, "P",     KA_YELLOW,  0x0D0D0Du);
        kd_itoa(sw.position, pbuf, 8);
        kd_str(cx + cw/2 - 26, road_bot + 24, pbuf,    KA_YELLOW,  0x0D0D0Du);
        kd_str(cx + cw/2 - 14, road_bot + 24, "/",     KA_DIM,    0x0D0D0Du);
        kd_itoa(1 + NUM_AI, pbuf, 8);
        kd_str(cx + cw/2 - 6,  road_bot + 24, pbuf,    KA_DIM,    0x0D0D0Du);
    }

    /* Race timer */
    {
        char tbuf[12];
        ticks_to_str(sw.race_ticks, tbuf, 12);
        kd_str(cx + cw - 132, road_bot + 8, "TIME", KA_DIM,   0x0D0D0Du);
        kd_str(cx + cw - 100, road_bot + 8, tbuf,   KA_TEXT,  0x0D0D0Du);
    }

    /* Best lap */
    if (sw.best_lap > 0) {
        char lbuf[12];
        ticks_to_str(sw.best_lap, lbuf, 12);
        kd_str(cx + cw - 132, road_bot + 24, "BEST", KA_DIM,    0x0D0D0Du);
        kd_str(cx + cw - 100, road_bot + 24, lbuf,   KA_BRIGHT, 0x0D0D0Du);
    }

    /* Controls hint */
    kd_str(cx + cw/2 - 148, road_bot + 46,
           "UP:accel  DN:brake  LEFT/RIGHT:steer  ESC:quit",
           KA_DIM, 0x0D0D0Du);

    /* ---- Top HUD strip ---- */
    kd_fill(cx, cy, cw, 14, 0x000A04u);
    kd_str(cx + 4, cy + 3, "SiMPLE SPEEDWAY", KA_BRIGHT, 0x000A04u);
    {
        /* Race position badge top-right */
        char pbuf[8] = "P";
        char pn[4]; kd_itoa(sw.position, pn, 4);
        int j = 1;
        for (int k = 0; pn[k]; k++) pbuf[j++] = pn[k];
        pbuf[j] = '\0';
        kd_str(cx + cw - 72, cy + 3, pbuf, KA_YELLOW, 0x000A04u);
    }

    /* ---- Lap flash (first 0.8 s after each lap) ---- */
    if (sw.phase == PHASE_RACING && sw.lap > 1 &&
        (sw.race_ticks - sw.lap_start) < 80) {
        char lf[16] = "LAP ";
        char ln[4]; kd_itoa(sw.lap, ln, 4);
        int j = 4; for (int k = 0; ln[k]; k++) lf[j++] = ln[k]; lf[j] = '\0';
        int tx = cx + cw/2 - 28;
        int ty = cy + ch / 4;
        kd_fill(tx - 8, ty - 4, 96, 20, 0x000000u);
        kd_str(tx, ty, lf, KA_BRIGHT, 0x000000u);
    }

    /* ---- Countdown overlay ---- */
    if (sw.phase == PHASE_COUNTDOWN) {
        const char *msg;
        uint32_t    msg_col;
        if      (sw.countdown > COUNTDOWN_DUR * 2 / 3) { msg = "3";   msg_col = KA_RED;    }
        else if (sw.countdown > COUNTDOWN_DUR / 3)      { msg = "2";   msg_col = KA_YELLOW; }
        else                                              { msg = "1";   msg_col = KA_BRIGHT; }

        int bx = cx + cw/2 - 36, by = cy + ch/3;
        kd_fill(bx,     by,     72, 56, 0x000000u);
        kd_rect(bx,     by,     72, 56, msg_col);
        /* Large text via offset-stacked single-character draws */
        for (int ddy = 0; ddy < 3; ddy++)
            for (int ddx = 0; ddx < 3; ddx++)
                kd_str(cx + cw/2 - 4 + ddx*2, by + 20 + ddy*2, msg, msg_col, 0x000000u);
    }

    /* ---- Finish overlay ---- */
    if (sw.phase == PHASE_FINISHED) {
        /* Scanline-darken the whole window */
        for (int rr = 0; rr < ch; rr += 2)
            kd_fill(cx, cy + rr, cw, 1, 0x000000u);

        int bx = cx + cw/4, by = cy + ch/6;
        int bw = cw/2,       bh = ch * 2 / 3;
        kd_fill(bx, by, bw, bh, 0x050E08u);
        kd_rect(bx, by, bw, bh, KA_BRIGHT);
        kd_fill(bx, by, bw, 18, 0x009940u);
        kd_str(bx + bw/2 - 60, by + 5, "RACE COMPLETE!", KA_WHITE, 0x009940u);

        int fy = by + 28;
        int fx = bx + 20;
        char buf[16];

        kd_str(fx, fy, "Final Position:", KA_DIM, 0x050E08u);
        kd_itoa(sw.finish_pos, buf, 8);
        kd_str(fx + 140, fy, buf, KA_YELLOW, 0x050E08u);
        {
            const char *rank =
                sw.finish_pos == 1 ? "(1st!)" :
                sw.finish_pos == 2 ? "(2nd)" :
                sw.finish_pos == 3 ? "(3rd)" : "(4th)";
            uint32_t rc =
                sw.finish_pos == 1 ? KA_BRIGHT :
                sw.finish_pos == 2 ? KA_TEXT   :
                sw.finish_pos == 3 ? KA_DIM    : KA_RED;
            kd_str(fx + 154, fy, rank, rc, 0x050E08u);
        }

        fy += 20;
        kd_str(fx, fy, "Total Time:", KA_DIM, 0x050E08u);
        ticks_to_str(sw.race_ticks, buf, 12);
        kd_str(fx + 140, fy, buf, KA_TEXT, 0x050E08u);

        fy += 20;
        kd_str(fx, fy, "Best Lap:", KA_DIM, 0x050E08u);
        if (sw.best_lap > 0) {
            ticks_to_str(sw.best_lap, buf, 12);
            kd_str(fx + 140, fy, buf, KA_BRIGHT, 0x050E08u);
        } else {
            kd_str(fx + 140, fy, "--:--.-", KA_DIM, 0x050E08u);
        }

        fy += 36;
        kd_str(fx, fy,      "R or click  =  restart race", KA_DIM,    0x050E08u);
        fy += 14;
        kd_str(fx, fy,      "ESC         =  quit to desktop", KA_DIM, 0x050E08u);
    }
}
