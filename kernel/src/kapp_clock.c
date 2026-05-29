#include "kapp.h"
#include "pit.h"
#include "string.h"

/* ================================================================
 * Clock — live uptime display
 * ================================================================ */

static void u32_str(uint32_t v, char *buf, int cap) {
    kd_utoa(v, buf, cap);
}

static void pad2(char *out, uint32_t v) {
    if (v < 10) { out[0] = '0'; out[1] = (char)('0' + v); out[2] = '\0'; }
    else { kd_utoa(v, out, 4); }
}

void clock_create(int wi)  { (void)wi; }
void clock_destroy(int wi) { (void)wi; }
void clock_key(int wi, int kt, char ch) { (void)wi;(void)kt;(void)ch; }
void clock_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void clock_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }
void clock_tick(int wi) { (void)wi; }

void clock_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi; (void)cw; (void)ch;

    kd_fill(cx, cy, cw, ch, KA_BG);

    /* Header bar */
    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "Clock", KA_HEADFG, KA_HEADBG);

    uint32_t ticks = pit_ticks();
    uint32_t secs  = ticks / 100u;
    uint32_t hh    = secs / 3600u;
    uint32_t mm    = (secs % 3600u) / 60u;
    uint32_t ss    = secs % 60u;

    /* Build HH:MM:SS string */
    char ts[12];
    char tmp[8];
    int  ti = 0;
    pad2(tmp, hh); ts[ti++] = tmp[0]; ts[ti++] = tmp[1];
    ts[ti++] = ':';
    pad2(tmp, mm); ts[ti++] = tmp[0]; ts[ti++] = tmp[1];
    ts[ti++] = ':';
    pad2(tmp, ss); ts[ti++] = tmp[0]; ts[ti++] = tmp[1];
    ts[ti] = '\0';

    kd_str(cx + 6, cy + 26, "Uptime:", KA_DIM, KA_BG);
    kd_str(cx + 6, cy + 38, ts, KA_BRIGHT, KA_BG);

    kd_str(cx + 6, cy + 56, "Ticks:", KA_DIM, KA_BG);
    u32_str(ticks, tmp, sizeof(tmp));
    kd_str(cx + 6, cy + 68, tmp, KA_BRIGHT, KA_BG);

    kd_str(cx + 6, cy + 86, "Days:", KA_DIM, KA_BG);
    u32_str(secs / 86400u, tmp, sizeof(tmp));
    kd_str(cx + 6, cy + 98, tmp, KA_BRIGHT, KA_BG);
}
