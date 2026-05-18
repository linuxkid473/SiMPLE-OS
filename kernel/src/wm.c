#include "wm.h"
#include "vga.h"
#include "keyboard.h"
#include "mouse.h"
#include "string.h"

/* ================================================================
 * Global window state
 * ================================================================ */
wm_window_t wm_windows[WM_MAX_WINDOWS];
int         wm_active       = 0;
int         wm_window_count = 0;

static int scr_w;
static int scr_h;

/* ================================================================
 * Drag state — mouse-driven window movement
 * ================================================================ */
static int drag_active  = 0;   /* 1 while left button held on a title bar */
static int drag_win_idx = -1;  /* index into wm_windows[]                 */
static int drag_off_x   = 0;   /* cursor-to-window-origin X offset        */
static int drag_off_y   = 0;   /* cursor-to-window-origin Y offset        */

/* ================================================================
 * Launcher bar state
 *
 * A fixed "Apps" button is always drawn in the top-left corner of
 * the desktop, on top of all windows (rendered last, hit-tested
 * first).  Clicking it toggles a small dropdown menu.
 * ================================================================ */
static int launcher_open = 0;

/* Launcher button — fixed screen position */
#define LNCHR_BTN_X     4
#define LNCHR_BTN_Y     4
#define LNCHR_BTN_W    40
#define LNCHR_BTN_H    16

/* Dropdown menu — immediately below the button */
#define LNCHR_MENU_X    4
#define LNCHR_MENU_Y   (LNCHR_BTN_Y + LNCHR_BTN_H + 1)   /* = 21 */
#define LNCHR_MENU_W  100
#define LNCHR_ITEM_H   16
#define LNCHR_NITEMS    1   /* just Calculator; add entries here for more apps */

/* ================================================================
 * Calculator button grid
 *
 * Positions are relative to the window's client-area top-left
 * (cx = w->x + WM_BORDER, cy = w->y + WM_TITLEBAR_H).
 *
 * Layout:
 *   row 0:  7   8   9   /
 *   row 1:  4   5   6   *
 *   row 2:  1   2   3   -
 *   row 3:  C   0   =   +
 * ================================================================ */
#define CALC_BTN_W    36    /* button width  in pixels */
#define CALC_BTN_H    22    /* button height in pixels */
#define CALC_BTN_GAP   2    /* gap between buttons     */
#define CALC_NCOLS     4
#define CALC_NROWS     4

/* left edge of column c within the client area */
#define CALC_COL(c)  (4 + (c) * (CALC_BTN_W + CALC_BTN_GAP))
/* top edge of row r within the client area (28 = 4 top-pad + 20 display + 4 gap) */
#define CALC_ROW(r)  (28 + (r) * (CALC_BTN_H + CALC_BTN_GAP))

typedef struct {
    int  rx, ry;       /* top-left relative to client-area origin */
    char label[4];     /* text drawn centred inside the button    */
    char action;       /* passed to wm_calc_handle_char() on click */
} calc_btn_t;

static const calc_btn_t calc_btns[CALC_NCOLS * CALC_NROWS] = {
    /* row 0 */
    { CALC_COL(0), CALC_ROW(0), "7", '7' },
    { CALC_COL(1), CALC_ROW(0), "8", '8' },
    { CALC_COL(2), CALC_ROW(0), "9", '9' },
    { CALC_COL(3), CALC_ROW(0), "/", '/' },
    /* row 1 */
    { CALC_COL(0), CALC_ROW(1), "4", '4' },
    { CALC_COL(1), CALC_ROW(1), "5", '5' },
    { CALC_COL(2), CALC_ROW(1), "6", '6' },
    { CALC_COL(3), CALC_ROW(1), "*", '*' },
    /* row 2 */
    { CALC_COL(0), CALC_ROW(2), "1", '1' },
    { CALC_COL(1), CALC_ROW(2), "2", '2' },
    { CALC_COL(2), CALC_ROW(2), "3", '3' },
    { CALC_COL(3), CALC_ROW(2), "-", '-' },
    /* row 3 */
    { CALC_COL(0), CALC_ROW(3), "C", 'C' },
    { CALC_COL(1), CALC_ROW(3), "0", '0' },
    { CALC_COL(2), CALC_ROW(3), "=", '=' },
    { CALC_COL(3), CALC_ROW(3), "+", '+' },
};

/* ================================================================
 * Colour palette
 * ================================================================ */
#define COL_DESKTOP       0x00001A   /* very dark navy desktop             */
#define COL_BORDER_ACT    0x888888   /* mid-grey border, focused window    */
#define COL_BORDER_INACT  0x333333   /* dark-grey border, unfocused        */
#define COL_TITLEBG_ACT   0x0000AA   /* classic blue title, focused        */
#define COL_TITLEBG_INACT 0x222255   /* dim blue title, unfocused          */
#define COL_TITLEFG       0xFFFFFF   /* white title text                   */
#define COL_CLIENTBG      0x000000   /* black client background            */
#define COL_DISPBG        0x003300   /* dark-green calculator display bg   */
#define COL_DISPFG        0x00FF00   /* bright-green calculator display fg */
#define COL_BTNBG         0x223355   /* calculator button background       */
#define COL_BTNBDR        0x4466AA   /* calculator button border           */
#define COL_BTNFG         0xFFFFFF   /* calculator button label text       */
#define COL_LNCHR_BG      0x333366   /* launcher button background         */
#define COL_LNCHR_BD      0x8888CC   /* launcher button border             */
#define COL_MENU_BG       0x1A1A33   /* launcher dropdown background       */
#define COL_MENU_BD       0x888888   /* launcher dropdown border           */
#define COL_MENU_FG       0xFFFFFF   /* launcher menu item text            */

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
    calc.left_val   = 0;
    calc.op         = 0;
    calc.num_buf[0] = '0';
    calc.num_len    = 1;
    calc.state      = 0;
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
        return;
    }
    if (calc.num_len < 10)
        calc.num_buf[calc.num_len++] = (char)('0' + d);
}

static void calc_operator(char op) {
    if (calc.state == 3) { calc_clear(); }

    if (calc.state == 1 && calc.num_len > 0) {
        calc_compute(calc_parse_num());
        if (calc.state == 3) return;
    } else if (calc.state == 0 && calc.num_len > 0) {
        calc.left_val = calc_parse_num();
    }

    calc.op      = op;
    calc.num_len = 0;
    calc.state   = 1;
}

static void calc_equals(void) {
    if (calc.state != 1 || calc.num_len == 0) return;
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
        calc.num_buf[0] = '0';
        calc.num_len    = 1;
    }
}

/* Public: keyboard input to calculator (also called on button click). */
void wm_calc_handle_char(char c) {
    if      (c == 'c' || c == 'C')                  calc_clear();
    else if (c >= '0' && c <= '9')                   calc_digit(c - '0');
    else if (c == '+' || c == '-' ||
             c == '*' || c == '/')                   calc_operator(c);
    else if (c == '=')                               calc_equals();
    else if (c == '\b')                              calc_backspace();

    wm_draw_all();
}

/* ================================================================
 * Window chrome rendering
 * ================================================================ */

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

static void sync_terminal_client(wm_window_t *w) {
    int cx = w->x + WM_BORDER;
    int cy = w->y + WM_TITLEBAR_H;
    int cw = w->width  - 2 * WM_BORDER;
    int ch = w->height - WM_TITLEBAR_H - WM_BORDER;
    vga_set_client(cx, cy, (uint32_t)(cw / 8), (uint32_t)(ch / 8));
}

/* ================================================================
 * Calculator GUI rendering
 * ================================================================ */

/* Draw one button: filled rect + 1-px border + centred label. */
static void draw_calc_button(int bx, int by, const char *label) {
    /* fill */
    fb_fill_rect(bx, by, CALC_BTN_W, CALC_BTN_H, COL_BTNBG);
    /* border — top, bottom, left, right */
    fb_fill_rect(bx,                  by,                  CALC_BTN_W, 1,          COL_BTNBDR);
    fb_fill_rect(bx,                  by + CALC_BTN_H - 1, CALC_BTN_W, 1,          COL_BTNBDR);
    fb_fill_rect(bx,                  by,                  1,          CALC_BTN_H, COL_BTNBDR);
    fb_fill_rect(bx + CALC_BTN_W - 1, by,                  1,          CALC_BTN_H, COL_BTNBDR);
    /* label — single character, centred inside the button */
    int tx = bx + (CALC_BTN_W - 8) / 2;
    int ty = by + (CALC_BTN_H - 8) / 2;
    fb_draw_string_px(tx, ty, label, COL_BTNFG, COL_BTNBG);
}

/* Draw the display area + full button grid inside the calculator window. */
static void draw_calc_content(wm_window_t *w) {
    int cx = w->x + WM_BORDER;
    int cy = w->y + WM_TITLEBAR_H;
    int cw = w->width - 2 * WM_BORDER;   /* = 158 */

    /* Display box: 4-px inset from client edges, 20 px tall */
    fb_fill_rect(cx + 4, cy + 4, cw - 8, 20, COL_DISPBG);
    char disp[32];
    calc_build_display(disp, (int)sizeof(disp));
    fb_draw_string_px(cx + 8, cy + 8, disp, COL_DISPFG, COL_DISPBG);

    /* Button grid */
    for (int i = 0; i < CALC_NCOLS * CALC_NROWS; i++)
        draw_calc_button(cx + calc_btns[i].rx,
                         cy + calc_btns[i].ry,
                         calc_btns[i].label);
}

/* ================================================================
 * Launcher bar rendering
 * ================================================================ */

static void draw_launcher(void) {
    /* "Apps" button — always visible */
    fb_fill_rect(LNCHR_BTN_X, LNCHR_BTN_Y, LNCHR_BTN_W, LNCHR_BTN_H, COL_LNCHR_BG);
    /* border */
    fb_fill_rect(LNCHR_BTN_X,                    LNCHR_BTN_Y,                   LNCHR_BTN_W, 1,            COL_LNCHR_BD);
    fb_fill_rect(LNCHR_BTN_X,                    LNCHR_BTN_Y + LNCHR_BTN_H - 1, LNCHR_BTN_W, 1,            COL_LNCHR_BD);
    fb_fill_rect(LNCHR_BTN_X,                    LNCHR_BTN_Y,                   1,            LNCHR_BTN_H, COL_LNCHR_BD);
    fb_fill_rect(LNCHR_BTN_X + LNCHR_BTN_W - 1, LNCHR_BTN_Y,                   1,            LNCHR_BTN_H, COL_LNCHR_BD);
    /* label */
    fb_draw_string_px(LNCHR_BTN_X + 4, LNCHR_BTN_Y + 4, "Apps", COL_TITLEFG, COL_LNCHR_BG);

    if (!launcher_open) return;

    /* Dropdown menu */
    int menu_h = LNCHR_NITEMS * LNCHR_ITEM_H + 2;   /* 1-px border top + bottom */
    fb_fill_rect(LNCHR_MENU_X, LNCHR_MENU_Y, LNCHR_MENU_W, menu_h, COL_MENU_BG);
    /* border */
    fb_fill_rect(LNCHR_MENU_X,                   LNCHR_MENU_Y,              LNCHR_MENU_W, 1,       COL_MENU_BD);
    fb_fill_rect(LNCHR_MENU_X,                   LNCHR_MENU_Y + menu_h - 1, LNCHR_MENU_W, 1,       COL_MENU_BD);
    fb_fill_rect(LNCHR_MENU_X,                   LNCHR_MENU_Y,              1,             menu_h, COL_MENU_BD);
    fb_fill_rect(LNCHR_MENU_X + LNCHR_MENU_W - 1, LNCHR_MENU_Y,            1,             menu_h, COL_MENU_BD);
    /* item 0: Calculator */
    fb_draw_string_px(LNCHR_MENU_X + 6, LNCHR_MENU_Y + 5,
                      "Calculator", COL_MENU_FG, COL_MENU_BG);
}

/* ================================================================
 * Mouse cursor
 * ================================================================ */

#define CUR_ARM 5

static void draw_cursor(int x, int y) {
    int len = CUR_ARM * 2 + 1;
    /* black outline */
    fb_fill_rect(x - CUR_ARM - 1, y - 1,           len + 2, 3,       0x000000);
    fb_fill_rect(x - 1,           y - CUR_ARM - 1, 3,       len + 2, 0x000000);
    /* white cross */
    fb_fill_rect(x - CUR_ARM, y,           len, 1, 0xFFFFFF);
    fb_fill_rect(x,           y - CUR_ARM, 1,   len, 0xFFFFFF);
}

/* ================================================================
 * Hit-testing helpers
 * ================================================================ */

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static int point_in_window(const wm_window_t *w, int px, int py) {
    return point_in_rect(px, py, w->x, w->y, w->width, w->height);
}

/* 1 if (px,py) is inside the title bar (top WM_TITLEBAR_H px of the window) */
static int point_in_titlebar(const wm_window_t *w, int px, int py) {
    return px >= w->x && px < w->x + w->width &&
           py >= w->y && py < w->y + WM_TITLEBAR_H;
}

/* ================================================================
 * Calculator launch helper
 * ================================================================ */

/* Make the calculator visible and bring it into focus.
 * If already visible, just focus it (don't re-position). */
static void wm_launch_calc(void) {
    /* Slot 1 is always reserved for the calculator */
    if (wm_windows[1].hidden) {
        wm_windows[1].hidden = 0;
        /* Centre on screen for a nice first appearance */
        wm_windows[1].x = (scr_w - WM_CALC_W) / 2;
        wm_windows[1].y = (scr_h - WM_CALC_H) / 2;
    }
    wm_active = 1;
}

/* ================================================================
 * Client-area click routing
 *
 * Called when a left-click lands inside a window but NOT in the
 * title bar.  Drag is never started from here.
 * ================================================================ */
static void handle_client_click(wm_window_t *w, int x, int y) {
    if (w->type != WM_TYPE_CALC) return;

    int cx = w->x + WM_BORDER;
    int cy = w->y + WM_TITLEBAR_H;

    for (int i = 0; i < CALC_NCOLS * CALC_NROWS; i++) {
        int bx = cx + calc_btns[i].rx;
        int by = cy + calc_btns[i].ry;
        if (x >= bx && x < bx + CALC_BTN_W &&
            y >= by && y < by + CALC_BTN_H) {
            /* wm_calc_handle_char updates calc state and calls wm_draw_all() */
            wm_calc_handle_char(calc_btns[i].action);
            return;
        }
    }
}

/* ================================================================
 * wm_handle_mouse
 *
 * Priority order for left-click:
 *   1. Launcher button   (always drawn on top → checked first)
 *   2. Launcher menu     (if open)
 *   3. Window hit-test   (z-order: active window first)
 *
 * Dragging: only initiated when click lands in a title bar.
 * Client-area clicks are routed to the app, never start a drag.
 * ================================================================ */
void wm_handle_mouse(int x, int y, uint8_t new_buttons, uint8_t prev_buttons) {
    int left_now  = (int)(new_buttons  & 1);
    int left_prev = (int)(prev_buttons & 1);

    if (left_now && !left_prev) {
        int launcher_handled = 0;

        /* ---- 1. Launcher button ---- */
        if (point_in_rect(x, y, LNCHR_BTN_X, LNCHR_BTN_Y,
                          LNCHR_BTN_W, LNCHR_BTN_H)) {
            launcher_open ^= 1;
            launcher_handled = 1;
        }

        /* ---- 2. Launcher menu (if open) ---- */
        if (!launcher_handled && launcher_open) {
            int menu_h = LNCHR_NITEMS * LNCHR_ITEM_H + 2;
            if (point_in_rect(x, y, LNCHR_MENU_X, LNCHR_MENU_Y,
                              LNCHR_MENU_W, menu_h)) {
                /* Which item did the user click? */
                int item = (y - LNCHR_MENU_Y - 1) / LNCHR_ITEM_H;
                if (item == 0) {        /* "Calculator" */
                    wm_launch_calc();
                }
                launcher_open = 0;
                launcher_handled = 1;
            } else {
                /* Click outside open menu → close it, fall through to windows */
                launcher_open = 0;
            }
        }

        /* ---- 3. Window hit-test in z-order ---- */
        if (!launcher_handled) {
            /* Check active window first (it's on top), then the rest */
            int order[WM_MAX_WINDOWS];
            int n = 0;
            order[n++] = wm_active;
            for (int i = 0; i < wm_window_count; i++)
                if (i != wm_active) order[n++] = i;

            for (int oi = 0; oi < n; oi++) {
                int i = order[oi];
                if (wm_windows[i].hidden) continue;
                if (!point_in_window(&wm_windows[i], x, y)) continue;

                wm_active = i;

                if (point_in_titlebar(&wm_windows[i], x, y)) {
                    /* Start drag — only title bar, never client area */
                    drag_active  = 1;
                    drag_win_idx = i;
                    drag_off_x   = x - wm_windows[i].x;
                    drag_off_y   = y - wm_windows[i].y;
                } else {
                    /* Client-area click → route to the app */
                    handle_client_click(&wm_windows[i], x, y);
                }
                break;
            }
        }
    }

    /* ---- Drag: move the grabbed window with the cursor ---- */
    if (drag_active && left_now) {
        wm_window_t *w = &wm_windows[drag_win_idx];
        w->x = x - drag_off_x;
        w->y = y - drag_off_y;
        if (w->x < 0)                w->x = 0;
        if (w->y < 0)                w->y = 0;
        if (w->x + w->width  > scr_w) w->x = scr_w - w->width;
        if (w->y + w->height > scr_h) w->y = scr_h - w->height;
    }

    /* ---- Button released → end drag ---- */
    if (!left_now && left_prev) drag_active = 0;

    wm_draw_all();
}

/* ================================================================
 * wm_draw_all — central repaint routine
 *
 * Rendering order:
 *   1. Desktop background fill
 *   2. Two-pass z-order for visible windows
 *        pass 0: every visible window EXCEPT the active one
 *        pass 1: the active window  (always appears on top)
 *   3. Launcher bar (drawn after windows so it's always accessible)
 *   4. Cursor (absolute topmost element)
 *
 * Guarantee: after this returns, vga_set_client() always reflects
 * the terminal window, so subsequent vga_putc() calls land there.
 * ================================================================ */
void wm_draw_all(void) {
    if (wm_window_count == 0) return;

    fb_fill_rect(0, 0, scr_w, scr_h, COL_DESKTOP);

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < wm_window_count; i++) {
            if (wm_windows[i].hidden) continue;           /* skip hidden */
            if ((i == wm_active) != (pass == 1)) continue;  /* z-order   */

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
     * vga_set_client guarantee:
     * The terminal is drawn in exactly one pass. sync_terminal_client()
     * is the last call for it. If the terminal is active (pass 1) it is
     * drawn last — client is set correctly. If it is inactive (pass 0)
     * it is drawn before the active window; the active window uses only
     * fb_fill_rect / fb_draw_string_px and never calls vga_set_client(),
     * so the terminal's settings survive. If the calculator is hidden,
     * the terminal is drawn in pass 1 as the sole visible window.
     */

    draw_launcher();                               /* always on top of windows */
    draw_cursor(mouse_get_x(), mouse_get_y());     /* absolute topmost         */
}

/* ================================================================
 * Public API
 * ================================================================ */

void wm_init(int sw, int sh) {
    scr_w         = sw;
    scr_h         = sh;
    launcher_open = 0;

    calc_clear();

    /* Window 0: Terminal — large, starts at top-left */
    wm_windows[0].x      = 2;
    wm_windows[0].y      = 2;
    wm_windows[0].width  = WM_TERM_W;
    wm_windows[0].height = WM_TERM_H;
    wm_windows[0].title  = "Terminal";
    wm_windows[0].type   = WM_TYPE_TERMINAL;
    wm_windows[0].hidden = 0;

    /* Window 1: Calculator — hidden at boot; launched via Apps menu.
     * Position is set when wm_launch_calc() first shows it. */
    wm_windows[1].x      = (sw - WM_CALC_W) / 2;
    wm_windows[1].y      = (sh - WM_CALC_H) / 2;
    wm_windows[1].width  = WM_CALC_W;
    wm_windows[1].height = WM_CALC_H;
    wm_windows[1].title  = "Calculator";
    wm_windows[1].type   = WM_TYPE_CALC;
    wm_windows[1].hidden = 1;    /* <-- not shown until Apps → Calculator */

    wm_active       = 0;   /* terminal has focus at boot */
    wm_window_count = 2;   /* total allocated slots; hidden ones still count */

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

    if (w->x < 0)               w->x = 0;
    if (w->y < 0)               w->y = 0;
    if (w->x + w->width  > scr_w) w->x = scr_w - w->width;
    if (w->y + w->height > scr_h) w->y = scr_h - w->height;

    wm_draw_all();
}

void wm_tab_switch(void) {
    /* Skip hidden windows so Alt+Tab only cycles visible ones. */
    int start = wm_active;
    int next  = (wm_active + 1) % wm_window_count;
    while (next != start && wm_windows[next].hidden)
        next = (next + 1) % wm_window_count;
    wm_active = next;   /* if all others are hidden, stays on current */
    wm_draw_all();
}

int wm_active_is_terminal(void) {
    /* Safety: if the active slot is somehow hidden, treat as terminal */
    if (wm_windows[wm_active].hidden) return 1;
    return wm_windows[wm_active].type == WM_TYPE_TERMINAL;
}
