#include "wm.h"
#include "vga.h"
#include "keyboard.h"
#include "string.h"

/* ---- global window state ---- */
wm_window_t wm_windows[WM_MAX_WINDOWS];
int         wm_active       = 0;
int         wm_window_count = 0;

static int scr_w;
static int scr_h;

/* ---- colour palette ---- */
#define COL_DESKTOP       0x00001A   /* very dark navy desktop             */
#define COL_BORDER_ACT    0x888888   /* mid-grey border, focused window    */
#define COL_BORDER_INACT  0x333333   /* dark-grey border, unfocused        */
#define COL_TITLEBG_ACT   0x0000AA   /* classic blue title, focused        */
#define COL_TITLEBG_INACT 0x222255   /* dim blue title, unfocused          */
#define COL_TITLEFG       0xFFFFFF   /* white title text                   */
#define COL_CLIENTBG      0x000000   /* black client background            */
#define COL_DISPBG        0x003300   /* dark-green calculator display bg   */
#define COL_DISPFG        0x00FF00   /* bright-green calculator display fg */
#define COL_HINTFG        0x666666   /* grey key-hint text                 */

/* ================================================================
 * Calculator state machine
 *
 * States:
 *   0 = ENTERING_LEFT  — user is typing the left operand
 *   1 = ENTERING_RIGHT — operator pressed; typing right operand
 *   2 = RESULT         — equals pressed; showing result
 *   3 = ERROR          — division by zero
 *
 * num_buf holds the digit characters of the number currently being
 * typed (max 10 digits).  Parsed to int32 only when needed.
 * ================================================================ */
typedef struct {
    int32_t left_val;       /* left operand (or result after =)    */
    char    op;             /* 0=none, '+', '-', '*', '/'          */
    char    num_buf[11];    /* digit chars being typed             */
    int     num_len;        /* valid chars in num_buf              */
    int     state;          /* 0-3 as above                        */
} calc_state_t;

static calc_state_t calc;

/* ---- calc helpers ---- */

/* Convert signed 32-bit integer to decimal string.  Returns length. */
static int i32_to_str(int32_t v, char *buf, int cap) {
    if (cap <= 1) return 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char     tmp[12];
    int      pos = 0;
    int      neg = (v < 0);
    uint32_t u   = neg ? ((v == (int32_t)0x80000000)
                              ? 2147483648U
                              : (uint32_t)(-v))
                       : (uint32_t)v;
    while (u > 0 && pos < 11) { tmp[pos++] = (char)('0' + u % 10); u /= 10; }
    int out = 0;
    if (neg && out + 1 < cap) buf[out++] = '-';
    while (pos > 0 && out + 1 < cap) buf[out++] = tmp[--pos];
    buf[out] = '\0';
    return out;
}

/* Append src to dst[dlen], bounded by cap.  Returns new length. */
static int str_cat(char *dst, int dlen, int cap, const char *src) {
    while (*src && dlen + 1 < cap) dst[dlen++] = *src++;
    dst[dlen] = '\0';
    return dlen;
}

/* Build the display string from current calculator state. */
static void calc_build_display(char *out, int cap) {
    char tmp[16];
    int  len = 0;
    out[0] = '\0';

    if (calc.state == 3) {
        str_cat(out, 0, cap, "ERR: div/0");
        return;
    }
    if (calc.state == 2) {                      /* show result */
        i32_to_str(calc.left_val, tmp, sizeof(tmp));
        str_cat(out, 0, cap, tmp);
        return;
    }
    if (calc.state == 0) {                      /* entering left */
        if (calc.num_len == 0) { out[0] = '0'; out[1] = '\0'; return; }
        for (int i = 0; i < calc.num_len && len + 1 < cap; i++)
            out[len++] = calc.num_buf[i];
        out[len] = '\0';
        return;
    }
    /* state == 1: entering right — show "left op [right]" */
    i32_to_str(calc.left_val, tmp, sizeof(tmp));
    len = str_cat(out, 0, cap, tmp);
    if (calc.op && len + 1 < cap) out[len++] = calc.op;
    out[len] = '\0';
    for (int i = 0; i < calc.num_len && len + 1 < cap; i++)
        out[len++] = calc.num_buf[i];
    out[len] = '\0';
}

static void calc_clear(void) {
    calc.left_val  = 0;
    calc.op        = 0;
    calc.num_buf[0]= '0';
    calc.num_len   = 1;
    calc.state     = 0;
}

static int32_t calc_parse_num(void) {
    int32_t v = 0;
    for (int i = 0; i < calc.num_len; i++)
        v = v * 10 + (calc.num_buf[i] - '0');
    return v;
}

static void calc_compute(int32_t right) {
    if (calc.op == '+') calc.left_val += right;
    else if (calc.op == '-') calc.left_val -= right;
    else if (calc.op == '*') calc.left_val *= right;
    else if (calc.op == '/') {
        if (right == 0) { calc.state = 3; return; }
        calc.left_val /= right;
    }
}

static void calc_digit(int d) {
    /* Fresh start after result or error */
    if (calc.state == 2 || calc.state == 3) calc_clear();

    /* Leading-zero suppression: "0" + non-zero → replace */
    if (calc.num_len == 1 && calc.num_buf[0] == '0') {
        if (d != 0) calc.num_buf[0] = (char)('0' + d);
        /* pressing '0' again while showing "0" → no-op */
        return;
    }
    if (calc.num_len < 10)
        calc.num_buf[calc.num_len++] = (char)('0' + d);
}

static void calc_operator(char op) {
    if (calc.state == 3) { calc_clear(); }   /* recover from error */

    /* Chain: if already entering the right operand, compute first */
    if (calc.state == 1 && calc.num_len > 0) {
        calc_compute(calc_parse_num());
        if (calc.state == 3) return;
    } else if (calc.state == 0 && calc.num_len > 0) {
        calc.left_val = calc_parse_num();    /* lock in the left value */
    }
    /* state==2: left_val already holds the previous result — chain it */

    calc.op      = op;
    calc.num_len = 0;
    calc.state   = 1;
}

static void calc_equals(void) {
    if (calc.state != 1 || calc.num_len == 0) return;   /* nothing to compute */
    calc_compute(calc_parse_num());
    if (calc.state != 3) {
        calc.op      = 0;
        calc.num_len = 0;
        calc.state   = 2;
    }
}

static void calc_backspace(void) {
    if (calc.state == 2 || calc.state == 3) { calc_clear(); return; }
    if (calc.num_len > 1) {
        calc.num_len--;
    } else {
        /* Reset to "0" rather than leaving the buffer empty */
        calc.num_buf[0] = '0';
        calc.num_len    = 1;
    }
}

void wm_calc_handle_char(char c) {
    if      (c == 'c' || c == 'C')                  calc_clear();
    else if (c >= '0' && c <= '9')                   calc_digit(c - '0');
    else if (c == '+' || c == '-' ||
             c == '*' || c == '/')                   calc_operator(c);
    else if (c == '=')                               calc_equals();
    else if (c == '\b')                              calc_backspace();
    /* Unknown characters are silently dropped */

    wm_draw_all();
}

/* ================================================================
 * Window chrome & content rendering
 * ================================================================ */

/* Draw border, title bar, and clear the client area for one window. */
static void draw_window_chrome(wm_window_t *w, int is_active) {
    int      wx = w->x, wy = w->y, ww = w->width, wh = w->height;
    uint32_t cb = is_active ? COL_BORDER_ACT   : COL_BORDER_INACT;
    uint32_t ct = is_active ? COL_TITLEBG_ACT  : COL_TITLEBG_INACT;

    /* Four-sided border */
    fb_fill_rect(wx,              wy,              ww,        WM_BORDER, cb);
    fb_fill_rect(wx,              wy+wh-WM_BORDER, ww,        WM_BORDER, cb);
    fb_fill_rect(wx,              wy,              WM_BORDER, wh,        cb);
    fb_fill_rect(wx+ww-WM_BORDER, wy,              WM_BORDER, wh,        cb);

    /* Title bar */
    fb_fill_rect(wx + WM_BORDER,
                 wy + WM_BORDER,
                 ww - 2*WM_BORDER,
                 WM_TITLEBAR_H - WM_BORDER,
                 ct);
    fb_draw_string_px(wx + WM_BORDER + 4,
                      wy + WM_BORDER + 3,
                      w->title, COL_TITLEFG, ct);

    /* Client background */
    fb_fill_rect(wx + WM_BORDER,
                 wy + WM_TITLEBAR_H,
                 ww - 2*WM_BORDER,
                 wh - WM_TITLEBAR_H - WM_BORDER,
                 COL_CLIENTBG);
}

/*
 * Tell vga.c where the terminal window's client area lives.
 * Must be called before vga_repaint_cells() or any vga_putc() that
 * should land in the terminal window.
 */
static void sync_terminal_client(wm_window_t *w) {
    int cx = w->x + WM_BORDER;
    int cy = w->y + WM_TITLEBAR_H;
    int cw = w->width  - 2 * WM_BORDER;
    int ch = w->height - WM_TITLEBAR_H - WM_BORDER;
    vga_set_client(cx, cy, (uint32_t)(cw / 8), (uint32_t)(ch / 8));
}

/* Draw the calculator's display and key-hint text inside its client area. */
static void draw_calc_content(wm_window_t *w) {
    int cx = w->x + WM_BORDER;
    int cy = w->y + WM_TITLEBAR_H;
    int cw = w->width - 2 * WM_BORDER;

    /* Display box — two character rows (16 px) in green-on-dark-green */
    fb_fill_rect(cx, cy, cw, 16, COL_DISPBG);
    char disp[40];
    calc_build_display(disp, (int)sizeof(disp));
    fb_draw_string_px(cx + 4, cy + 4, disp, COL_DISPFG, COL_DISPBG);

    /* Static key-hint rows below the display */
    fb_draw_string_px(cx + 4, cy + 22, "0-9  +  -  *  /", COL_HINTFG, COL_CLIENTBG);
    fb_draw_string_px(cx + 4, cy + 32, "=  C(clear)  Bksp",  COL_HINTFG, COL_CLIENTBG);
    fb_draw_string_px(cx + 4, cy + 50, "Alt+Tab: switch", COL_HINTFG, COL_CLIENTBG);
    fb_draw_string_px(cx + 4, cy + 60, "Alt+Arrows: move",   COL_HINTFG, COL_CLIENTBG);
}

/* ================================================================
 * wm_draw_all — the central repaint routine.
 *
 * Two-pass z-order:
 *   pass 0: draw every window EXCEPT the active one
 *   pass 1: draw the active window on top
 *
 * After both passes the vga text API (draw_off_x/y, fb_cols/rows) is
 * anchored to the terminal window, regardless of which window is
 * active.  This means shell vga_putc() calls always go to the right
 * place, even when the calculator is focused.
 * ================================================================ */
void wm_draw_all(void) {
    if (wm_window_count == 0) return;

    /* Erase the old chrome (covers the whole desktop once per frame) */
    fb_fill_rect(0, 0, scr_w, scr_h, COL_DESKTOP);

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < wm_window_count; i++) {
            /* pass 0 = inactive only, pass 1 = active only */
            if ((i == wm_active) != (pass == 1)) continue;

            wm_window_t *w = &wm_windows[i];
            draw_window_chrome(w, (i == wm_active));

            if (w->type == WM_TYPE_TERMINAL) {
                sync_terminal_client(w);
                vga_repaint_cells();
            } else if (w->type == WM_TYPE_CALC) {
                draw_calc_content(w);
            }
        }
    }
    /*
     * Guarantee: after this function returns, vga_set_client() reflects
     * the terminal window.
     *
     * Proof: the terminal is always drawn in exactly one pass.  When it
     * is drawn, sync_terminal_client() is the last thing called for it.
     * If the terminal is active (pass 1) it is drawn last, so the state
     * is correct after the loops.  If the terminal is inactive (pass 0)
     * it is drawn before the active window; the active window (calc)
     * uses only raw fb calls and never touches vga_set_client(), so the
     * terminal's client settings are preserved.
     */
}

/* ================================================================
 * Public API
 * ================================================================ */

void wm_init(int sw, int sh) {
    scr_w = sw;
    scr_h = sh;

    calc_clear();

    /* Window 0: Terminal — large, top-left */
    wm_windows[0].x      = 2;
    wm_windows[0].y      = 2;
    wm_windows[0].width  = WM_TERM_W;
    wm_windows[0].height = WM_TERM_H;
    wm_windows[0].title  = "Terminal";
    wm_windows[0].type   = WM_TYPE_TERMINAL;

    /* Window 1: Calculator — below the terminal */
    wm_windows[1].x      = 2;
    wm_windows[1].y      = wm_windows[0].y + wm_windows[0].height + 4;
    wm_windows[1].width  = WM_CALC_W;
    wm_windows[1].height = WM_CALC_H;
    wm_windows[1].title  = "Calculator";
    wm_windows[1].type   = WM_TYPE_CALC;

    wm_active       = 0;   /* terminal has focus at boot */
    wm_window_count = 2;

    wm_draw_all();
}

void wm_handle_key(int key_type) {
    wm_window_t *w = &wm_windows[wm_active];
    int dx = 0, dy = 0;

    if      (key_type == KEY_EVENT_LEFT)  dx = -WM_MOVE_STEP;
    else if (key_type == KEY_EVENT_RIGHT) dx =  WM_MOVE_STEP;
    else if (key_type == KEY_EVENT_UP)    dy = -WM_MOVE_STEP;
    else if (key_type == KEY_EVENT_DOWN)  dy =  WM_MOVE_STEP;

    w->x += dx;
    w->y += dy;

    /* Clamp so the window stays fully on screen */
    if (w->x < 0)               w->x = 0;
    if (w->y < 0)               w->y = 0;
    if (w->x + w->width  > scr_w) w->x = scr_w - w->width;
    if (w->y + w->height > scr_h) w->y = scr_h - w->height;

    wm_draw_all();
}

void wm_tab_switch(void) {
    /* Cycle to the next window in the array */
    wm_active = (wm_active + 1) % wm_window_count;
    wm_draw_all();
}

int wm_active_is_terminal(void) {
    return wm_windows[wm_active].type == WM_TYPE_TERMINAL;
}
