#include "kapp.h"
#include "string.h"

/* ================================================================
 * Settings
 * ================================================================ */

static const char *opt_names[] = {
    "Window style:", "Font size:", "Show clock:", "Mouse speed:",
};
static const char *opt_vals[][4] = {
    { "Green Glass", "Cyan Glass", "Amber", "Mono" },
    { "8x8", "Small", NULL, NULL },
    { "Off", "Uptime", NULL, NULL },
    { "Slow", "Normal", "Fast", NULL },
};
static const int opt_nchoices[] = { 4, 2, 2, 3 };
static int opt_cur[] = { 0, 0, 0, 1 };

#define ST_NOPT  4

static int st_sel = 0;

void settings_create(int wi)  { (void)wi; st_sel = 0; }
void settings_destroy(int wi) { (void)wi; }
void settings_tick(int wi)    { (void)wi; }
void settings_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void settings_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void settings_key(int wi, int kt, char ch) {
    (void)wi; (void)ch;
    if (kt == KEY_EVENT_UP   && st_sel > 0)             st_sel--;
    if (kt == KEY_EVENT_DOWN && st_sel < ST_NOPT - 1)   st_sel++;
    if (kt == KEY_EVENT_LEFT  && opt_cur[st_sel] > 0)                          opt_cur[st_sel]--;
    if (kt == KEY_EVENT_RIGHT && opt_cur[st_sel] < opt_nchoices[st_sel] - 1)   opt_cur[st_sel]++;
}

void settings_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, KA_BG);

    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "Settings", KA_HEADFG, KA_HEADBG);

    int y = cy + 28;
    for (int i = 0; i < ST_NOPT; i++) {
        int sel = (i == st_sel);
        uint32_t bg = sel ? KA_SELBG : KA_BG;
        uint32_t fg = sel ? KA_SELFG : KA_TEXT;

        kd_fill(cx, y - 2, cw, 18, bg);
        kd_str(cx + 6, y + 2, opt_names[i], fg, bg);

        /* Show < value > */
        int vx = cx + 150;
        kd_str(vx, y + 2, sel ? "< " : "  ", KA_BRIGHT, bg);
        kd_str(vx + 16, y + 2, opt_vals[i][opt_cur[i]], sel ? KA_YELLOW : KA_DIM, bg);
        int vend = vx + 16 + (int)strlen(opt_vals[i][opt_cur[i]]) * 8 + 4;
        if (opt_cur[i] < opt_nchoices[i] - 1)
            kd_str(vend, y + 2, sel ? " >" : "  ", KA_BRIGHT, bg);

        kd_hline(cx, y + 16, cw, KA_BORDER);
        y += 22;
    }

    kd_fill(cx, cy + ch - 14, cw, 14, KA_HEADBG);
    kd_str(cx + 6, cy + ch - 9, "Up/Down: select   Left/Right: change", KA_DIM, KA_HEADBG);
}
