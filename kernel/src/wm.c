#include "wm.h"
#include "vga.h"
#include "keyboard.h"
#include "mouse.h"
#include "string.h"

/* ================================================================
 * Global window state
 * ================================================================ */
wm_window_t wm_windows[WM_MAX_WINDOWS];
int         wm_active = 0;

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
#define LNCHR_NITEMS    3   /* STerm, Calculator, SText; add entries here for more apps */

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
#define COL_CLOSEBTN_BG   0x882222   /* close button background — dark red */
#define COL_CLOSEBTN_BD   0xBB4444   /* close button border                */
#define COL_LNCHR_BG      0x333366   /* launcher button background         */
#define COL_LNCHR_BD      0x8888CC   /* launcher button border             */
#define COL_MENU_BG       0x1A1A33   /* launcher dropdown background       */
#define COL_MENU_BD       0x888888   /* launcher dropdown border           */
#define COL_MENU_FG       0xFFFFFF   /* launcher menu item text            */
#define COL_STEXT_FG      0xCCCCCC   /* SText editor text colour           */

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

/* Instance pools — fixed arrays, no heap.
 * 'used[i]' tracks whether slot i is live; the window's .instance field
 * is the index into the matching pool. */
static calc_state_t calc_instances[WM_MAX_CALC_INST];
static int          calc_used[WM_MAX_CALC_INST];
static calc_state_t *calc;   /* points to the currently-active instance */

static term_session_t term_sessions[WM_MAX_TERM_INST];
static int            term_used[WM_MAX_TERM_INST];

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

    if (calc->state == 3) {
        str_cat(out, 0, cap, "ERR: div/0");
        return;
    }
    if (calc->state == 2) {                      /* show result */
        i32_to_str(calc->left_val, tmp, sizeof(tmp));
        str_cat(out, 0, cap, tmp);
        return;
    }
    if (calc->state == 0) {                      /* entering left */
        if (calc->num_len == 0) { out[0] = '0'; out[1] = '\0'; return; }
        for (int i = 0; i < calc->num_len && len + 1 < cap; i++)
            out[len++] = calc->num_buf[i];
        out[len] = '\0';
        return;
    }
    /* state == 1: entering right — show "left op [right]" */
    i32_to_str(calc->left_val, tmp, sizeof(tmp));
    len = str_cat(out, 0, cap, tmp);
    if (calc->op && len + 1 < cap) out[len++] = calc->op;
    out[len] = '\0';
    for (int i = 0; i < calc->num_len && len + 1 < cap; i++)
        out[len++] = calc->num_buf[i];
    out[len] = '\0';
}

static void calc_clear_inst(calc_state_t *c) {
    c->left_val   = 0;
    c->op         = 0;
    c->num_buf[0] = '0';
    c->num_len    = 1;
    c->state      = 0;
}

static void calc_clear(void) { calc_clear_inst(calc); }

static int32_t calc_parse_num(void) {
    int32_t v = 0;
    for (int i = 0; i < calc->num_len; i++)
        v = v * 10 + (calc->num_buf[i] - '0');
    return v;
}

static void calc_compute(int32_t right) {
    if (calc->op == '+') calc->left_val += right;
    else if (calc->op == '-') calc->left_val -= right;
    else if (calc->op == '*') calc->left_val *= right;
    else if (calc->op == '/') {
        if (right == 0) { calc->state = 3; return; }
        calc->left_val /= right;
    }
}

static void calc_digit(int d) {
    /* Fresh start after result or error */
    if (calc->state == 2 || calc->state == 3) calc_clear();

    /* Leading-zero suppression: "0" + non-zero → replace */
    if (calc->num_len == 1 && calc->num_buf[0] == '0') {
        if (d != 0) calc->num_buf[0] = (char)('0' + d);
        return;
    }
    if (calc->num_len < 10)
        calc->num_buf[calc->num_len++] = (char)('0' + d);
}

static void calc_operator(char op) {
    if (calc->state == 3) { calc_clear(); }

    if (calc->state == 1 && calc->num_len > 0) {
        calc_compute(calc_parse_num());
        if (calc->state == 3) return;
    } else if (calc->state == 0 && calc->num_len > 0) {
        calc->left_val = calc_parse_num();
    }

    calc->op      = op;
    calc->num_len = 0;
    calc->state   = 1;
}

static void calc_equals(void) {
    if (calc->state != 1 || calc->num_len == 0) return;
    calc_compute(calc_parse_num());
    if (calc->state != 3) {
        calc->op      = 0;
        calc->num_len = 0;
        calc->state   = 2;
    }
}

static void calc_backspace(void) {
    if (calc->state == 2 || calc->state == 3) { calc_clear(); return; }
    if (calc->num_len > 1) {
        calc->num_len--;
    } else {
        calc->num_buf[0] = '0';
        calc->num_len    = 1;
    }
}

/* Public: keyboard input to calculator (also called on button click). */
void wm_calc_handle_char(char c) {
    calc = &calc_instances[wm_windows[wm_active].instance];
    if      (c == 'c' || c == 'C')                  calc_clear();
    else if (c >= '0' && c <= '9')                   calc_digit(c - '0');
    else if (c == '+' || c == '-' ||
             c == '*' || c == '/')                   calc_operator(c);
    else if (c == '=')                               calc_equals();
    else if (c == '\b')                              calc_backspace();

    wm_draw_all();
}

/* ================================================================
 * SText — simple text editor state
 *
 * Each SText instance has its own stext_inst_t.  The static pointer
 * `si` is updated before any editor operation or render call to point
 * at the right instance, so all the helper functions below remain
 * unchanged.
 *
 * Window client area: 44 visible columns × 16 visible rows.
 * Up to STEXT_MAX_ROWS lines stored per instance.
 * ================================================================ */
#define STEXT_VIS_COLS  44
#define STEXT_VIS_ROWS  16
#define STEXT_MAX_ROWS  64

typedef struct {
    char buf[STEXT_MAX_ROWS][STEXT_VIS_COLS + 1];
    int  nlines, cx, cy, scroll;
} stext_inst_t;

static stext_inst_t stext_instances[WM_MAX_STEXT_INST];
static int          stext_used[WM_MAX_STEXT_INST];
static stext_inst_t *si;   /* points to the currently-active stext instance */

static void stext_init_inst(stext_inst_t *s) {
    int i;
    for (i = 0; i < STEXT_MAX_ROWS; i++) s->buf[i][0] = '\0';
    s->nlines = 1;
    s->cx = s->cy = s->scroll = 0;
}

/* After moving the cursor, ensure it stays within the visible band. */
static void stext_clamp_scroll(void) {
    if (si->cy < si->scroll)
        si->scroll = si->cy;
    if (si->cy >= si->scroll + STEXT_VIS_ROWS)
        si->scroll = si->cy - STEXT_VIS_ROWS + 1;
}

/* Copy row src into row dst (includes NUL). */
static void stext_copy_row(int dst, int src) {
    int j;
    for (j = 0; j <= STEXT_VIS_COLS; j++)
        si->buf[dst][j] = si->buf[src][j];
}

static void stext_insert_char(char c) {
    int len = (int)strlen(si->buf[si->cy]);
    int i;
    if (len >= STEXT_VIS_COLS) return;   /* line full — silently clamp */
    for (i = len; i >= si->cx; i--)
        si->buf[si->cy][i + 1] = si->buf[si->cy][i];
    si->buf[si->cy][si->cx] = c;
    si->cx++;
}

static void stext_backspace(void) {
    int len, prev_len, cur_len, i;
    if (si->cx > 0) {
        len = (int)strlen(si->buf[si->cy]);
        for (i = si->cx - 1; i < len; i++)
            si->buf[si->cy][i] = si->buf[si->cy][i + 1];
        si->cx--;
    } else if (si->cy > 0) {
        prev_len = (int)strlen(si->buf[si->cy - 1]);
        cur_len  = (int)strlen(si->buf[si->cy]);
        if (prev_len + cur_len <= STEXT_VIS_COLS) {
            /* Append current line onto previous line */
            for (i = 0; i <= cur_len; i++)
                si->buf[si->cy - 1][prev_len + i] = si->buf[si->cy][i];
            /* Shift remaining lines up */
            for (i = si->cy; i + 1 < si->nlines; i++)
                stext_copy_row(i, i + 1);
            si->buf[si->nlines - 1][0] = '\0';
            si->nlines--;
            si->cy--;
            si->cx = prev_len;
            stext_clamp_scroll();
        }
        /* If lines would overflow after merge, do nothing (safe no-op) */
    }
}

static void stext_newline(void) {
    int cx = si->cx;
    int cy = si->cy;
    int tail, i;
    if (si->nlines >= STEXT_MAX_ROWS) return;
    /* Shift lines below the cursor down by one */
    for (i = si->nlines; i > cy + 1; i--)
        stext_copy_row(i, i - 1);
    /* New line = tail of the current line from cursor onward */
    tail = (int)strlen(si->buf[cy]) - cx;
    for (i = 0; i <= tail; i++)
        si->buf[cy + 1][i] = si->buf[cy][cx + i];
    /* Truncate current line at cursor */
    si->buf[cy][cx] = '\0';
    si->nlines++;
    si->cy++;
    si->cx = 0;
    stext_clamp_scroll();
}

static void stext_delete_fwd(void) {
    int len = (int)strlen(si->buf[si->cy]);
    int i;
    if (si->cx < len) {
        for (i = si->cx; i < len; i++)
            si->buf[si->cy][i] = si->buf[si->cy][i + 1];
    }
    /* Delete at EOL with no line merge — keep it simple */
}

static void stext_move(int key_type) {
    int new_len;
    if (key_type == KEY_EVENT_LEFT) {
        if (si->cx > 0) {
            si->cx--;
        } else if (si->cy > 0) {
            si->cy--;
            si->cx = (int)strlen(si->buf[si->cy]);
        }
    } else if (key_type == KEY_EVENT_RIGHT) {
        int len = (int)strlen(si->buf[si->cy]);
        if (si->cx < len) {
            si->cx++;
        } else if (si->cy + 1 < si->nlines) {
            si->cy++;
            si->cx = 0;
        }
    } else if (key_type == KEY_EVENT_UP) {
        if (si->cy > 0) {
            si->cy--;
            new_len = (int)strlen(si->buf[si->cy]);
            if (si->cx > new_len) si->cx = new_len;
        }
    } else if (key_type == KEY_EVENT_DOWN) {
        if (si->cy + 1 < si->nlines) {
            si->cy++;
            new_len = (int)strlen(si->buf[si->cy]);
            if (si->cx > new_len) si->cx = new_len;
        }
    }
    stext_clamp_scroll();
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

    /* Close button — 12×12 px, top-right of title bar */
    int cbx = wx + ww - 16;
    int cby = wy + 4;
    fb_fill_rect(cbx,      cby,      12, 12, COL_CLOSEBTN_BG);
    fb_fill_rect(cbx,      cby,      12, 1,  COL_CLOSEBTN_BD);
    fb_fill_rect(cbx,      cby + 11, 12, 1,  COL_CLOSEBTN_BD);
    fb_fill_rect(cbx,      cby,      1,  12, COL_CLOSEBTN_BD);
    fb_fill_rect(cbx + 11, cby,      1,  12, COL_CLOSEBTN_BD);
    fb_draw_string_px(cbx + 2, cby + 2, "X", COL_TITLEFG, COL_CLOSEBTN_BG);

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

    calc = &calc_instances[w->instance];   /* select this window's calc state */

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
 * SText GUI rendering
 * ================================================================ */

static void draw_stext_content(wm_window_t *w) {
    /* Text area: 4-px padding inside client area, 8 px per character cell */
    int cx = w->x + WM_BORDER + 4;
    int cy = w->y + WM_TITLEBAR_H + 4;
    int r, line_idx;

    si = &stext_instances[w->instance];    /* select this window's editor state */

    /* Draw each visible row */
    for (r = 0; r < STEXT_VIS_ROWS; r++) {
        line_idx = si->scroll + r;
        int ty   = cy + r * 8;
        if (line_idx < si->nlines)
            fb_draw_string_px(cx, ty, si->buf[line_idx], COL_STEXT_FG, COL_CLIENTBG);
        /* Lines below si->nlines are already black from chrome fill — skip */
    }

    /* Cursor: invert the character cell under the cursor */
    int vis_row = si->cy - si->scroll;
    if (vis_row >= 0 && vis_row < STEXT_VIS_ROWS) {
        char cur_ch[2];
        cur_ch[0] = si->buf[si->cy][si->cx];
        if (!cur_ch[0]) cur_ch[0] = ' ';
        cur_ch[1] = '\0';
        fb_draw_string_px(cx + si->cx * 8, cy + vis_row * 8,
                          cur_ch, COL_CLIENTBG, COL_STEXT_FG);
    }
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
    /* item 0: STerm */
    fb_draw_string_px(LNCHR_MENU_X + 6, LNCHR_MENU_Y + 1 + 4,
                      "STerm", COL_MENU_FG, COL_MENU_BG);
    /* item 1: Calculator */
    fb_draw_string_px(LNCHR_MENU_X + 6, LNCHR_MENU_Y + 1 + LNCHR_ITEM_H + 4,
                      "Calculator", COL_MENU_FG, COL_MENU_BG);
    /* item 2: SText */
    fb_draw_string_px(LNCHR_MENU_X + 6, LNCHR_MENU_Y + 1 + 2 * LNCHR_ITEM_H + 4,
                      "SText", COL_MENU_FG, COL_MENU_BG);
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

/* 1 if (px,py) is inside the close button (12×12 at top-right of title bar) */
static int point_in_close_btn(const wm_window_t *w, int px, int py) {
    return point_in_rect(px, py, w->x + w->width - 16, w->y + 4, 12, 12);
}

/* ================================================================
 * Window / instance lifecycle helpers
 * ================================================================ */

/* Find a free slot in a used[] array of length max.  Returns index or -1. */
static int alloc_inst(int *used, int max) {
    int i;
    for (i = 0; i < max; i++)
        if (!used[i]) { used[i] = 1; return i; }
    return -1;
}

static void free_inst(int *used, int idx) { used[idx] = 0; }

/*
 * Change focus to new_idx, saving / restoring terminal sessions as needed.
 * Always safe to call even when old wm_active is hidden.
 */
static void wm_set_active(int new_idx) {
    /* Save outgoing terminal session into its struct */
    if (!wm_windows[wm_active].hidden &&
        wm_windows[wm_active].type == WM_TYPE_TERMINAL)
        vga_save_session(&term_sessions[wm_windows[wm_active].instance]);

    wm_active = new_idx;

    /* Restore incoming terminal session into globals */
    if (!wm_windows[new_idx].hidden &&
        wm_windows[new_idx].type == WM_TYPE_TERMINAL) {
        vga_restore_session(&term_sessions[wm_windows[new_idx].instance]);
        sync_terminal_client(&wm_windows[new_idx]);
    }
}

/*
 * Spawn a new window of the given type.  Finds a free window slot and a
 * free instance slot; initialises both; transfers focus.  Silent no-op
 * if either pool is exhausted.
 */
static void wm_spawn(wm_win_type_t type) {
    int wi, inst, offset;
    wm_window_t *w;

    /* Find a free window slot (hidden == 1 means free) */
    wi = -1;
    { int i; for (i = 0; i < WM_MAX_WINDOWS; i++)
        if (wm_windows[i].hidden) { wi = i; break; } }
    if (wi < 0) return;

    /* Allocate an instance slot and initialise it */
    if (type == WM_TYPE_TERMINAL) {
        inst = alloc_inst(term_used, WM_MAX_TERM_INST);
        if (inst < 0) return;
        vga_init_session(&term_sessions[inst]);
    } else if (type == WM_TYPE_CALC) {
        inst = alloc_inst(calc_used, WM_MAX_CALC_INST);
        if (inst < 0) return;
        calc_clear_inst(&calc_instances[inst]);
    } else {
        inst = alloc_inst(stext_used, WM_MAX_STEXT_INST);
        if (inst < 0) return;
        stext_init_inst(&stext_instances[inst]);
    }

    /* Cascade multiple windows of the same type so they don't overlap exactly */
    offset = inst * 24;

    w           = &wm_windows[wi];
    w->type     = type;
    w->instance = inst;
    w->hidden   = 0;

    if (type == WM_TYPE_TERMINAL) {
        w->x = 2 + offset;  w->y = 2 + offset;
        w->width = WM_TERM_W;  w->height = WM_TERM_H;
        w->title = "STerm";
    } else if (type == WM_TYPE_CALC) {
        w->x = (scr_w - WM_CALC_W)  / 2 + offset;
        w->y = (scr_h - WM_CALC_H)  / 2 + offset;
        w->width = WM_CALC_W;  w->height = WM_CALC_H;
        w->title = "Calculator";
    } else {
        w->x = (scr_w - WM_STEXT_W) / 2 + offset;
        w->y = (scr_h - WM_STEXT_H) / 2 + offset;
        w->width = WM_STEXT_W;  w->height = WM_STEXT_H;
        w->title = "SText";
    }

    wm_set_active(wi);
}

/*
 * Close a window: free its instance slot, hide it, transfer focus.
 */
static void wm_close_window(int idx) {
    wm_window_t *w = &wm_windows[idx];

    /* Free the instance back to its pool */
    if (w->type == WM_TYPE_TERMINAL)
        free_inst(term_used, w->instance);
    else if (w->type == WM_TYPE_CALC)
        free_inst(calc_used, w->instance);
    else
        free_inst(stext_used, w->instance);

    w->hidden = 1;

    /* Cancel any active drag on this window */
    if (drag_win_idx == idx) { drag_active = 0; drag_win_idx = -1; }

    /* Transfer focus if this was the active window */
    if (wm_active == idx) {
        int i, found = -1;
        for (i = 0; i < WM_MAX_WINDOWS; i++)
            if (!wm_windows[i].hidden) { found = i; break; }
        /* If no visible window exists, keep wm_active = idx (now hidden);
         * wm_active_is_terminal() returns 1 for hidden → input safe. */
        if (found >= 0)
            wm_set_active(found);
    }
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

    /* Point calc at this window's instance before dispatching */
    calc = &calc_instances[w->instance];

    for (int i = 0; i < CALC_NCOLS * CALC_NROWS; i++) {
        int bx = cx + calc_btns[i].rx;
        int by = cy + calc_btns[i].ry;
        if (x >= bx && x < bx + CALC_BTN_W &&
            y >= by && y < by + CALC_BTN_H) {
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
                if (item == 0) {        /* "STerm" */
                    wm_spawn(WM_TYPE_TERMINAL);
                } else if (item == 1) { /* "Calculator" */
                    wm_spawn(WM_TYPE_CALC);
                } else if (item == 2) { /* "SText" */
                    wm_spawn(WM_TYPE_STEXT);
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
            for (int i = 0; i < WM_MAX_WINDOWS; i++)
                if (i != wm_active) order[n++] = i;

            for (int oi = 0; oi < n; oi++) {
                int i = order[oi];
                if (wm_windows[i].hidden) continue;
                if (!point_in_window(&wm_windows[i], x, y)) continue;

                wm_set_active(i);

                if (point_in_titlebar(&wm_windows[i], x, y)) {
                    if (point_in_close_btn(&wm_windows[i], x, y)) {
                        /* Close button — hide window and transfer focus */
                        wm_close_window(i);
                    } else {
                        /* Start drag — only title bar, never client area */
                        drag_active  = 1;
                        drag_win_idx = i;
                        drag_off_x   = x - wm_windows[i].x;
                        drag_off_y   = y - wm_windows[i].y;
                    }
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
    fb_fill_rect(0, 0, scr_w, scr_h, COL_DESKTOP);

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (wm_windows[i].hidden) continue;
            if ((i == wm_active) != (pass == 1)) continue;

            wm_window_t *w = &wm_windows[i];
            draw_window_chrome(w, (i == wm_active));

            if (w->type == WM_TYPE_TERMINAL) {
                if (i == wm_active) {
                    /*
                     * Active terminal: globals already hold this session's
                     * state (maintained by wm_set_active).  Just update the
                     * draw offset in case the window was moved, then repaint.
                     */
                    sync_terminal_client(w);
                    vga_repaint_cells();
                } else {
                    /*
                     * Inactive terminal: paint directly from the saved session
                     * without touching global state, so the active terminal's
                     * cursor / cell buffer are preserved.
                     */
                    term_session_t *ts = &term_sessions[w->instance];
                    ts->draw_off_x = w->x + WM_BORDER;
                    ts->draw_off_y = w->y + WM_TITLEBAR_H;
                    ts->fb_cols    = (uint32_t)((w->width  - 2*WM_BORDER) / 8);
                    ts->fb_rows    = (uint32_t)((w->height - WM_TITLEBAR_H - WM_BORDER) / 8);
                    vga_repaint_session(ts);
                }
            } else if (w->type == WM_TYPE_CALC) {
                draw_calc_content(w);
            } else if (w->type == WM_TYPE_STEXT) {
                draw_stext_content(w);
            }
        }
    }

    /*
     * vga_set_client guarantee:
     * wm_set_active() calls sync_terminal_client() whenever focus moves to
     * a terminal, so globals always reflect the focused terminal.  The
     * active terminal also calls sync_terminal_client() above to refresh the
     * draw offset if the window moved.  Non-terminal focus paths leave the
     * last active terminal's settings intact so vga_putc still works.
     */

    draw_launcher();
    draw_cursor(mouse_get_x(), mouse_get_y());
}

/* ================================================================
 * Public API
 * ================================================================ */

void wm_init(int sw, int sh) {
    int i;
    scr_w         = sw;
    scr_h         = sh;
    launcher_open = 0;

    /* Mark all window slots as free */
    for (i = 0; i < WM_MAX_WINDOWS; i++) wm_windows[i].hidden = 1;

    /* Clear all instance pools */
    for (i = 0; i < WM_MAX_TERM_INST;  i++) { term_used[i]  = 0; vga_init_session(&term_sessions[i]); }
    for (i = 0; i < WM_MAX_CALC_INST;  i++) { calc_used[i]  = 0; calc_clear_inst(&calc_instances[i]); }
    for (i = 0; i < WM_MAX_STEXT_INST; i++) { stext_used[i] = 0; stext_init_inst(&stext_instances[i]); }

    /* Safe default current-instance pointers (updated by wm_spawn / key handlers) */
    calc = &calc_instances[0];
    si   = &stext_instances[0];

    /* wm_active must be a valid index before wm_set_active's save check runs */
    wm_active = 0;

    /* Auto-spawn the initial terminal so the shell has somewhere to write */
    wm_spawn(WM_TYPE_TERMINAL);

    wm_draw_all();
}

void wm_handle_key(int key_type) {
    /* Don't try to move a hidden window */
    if (wm_windows[wm_active].hidden) {
        wm_draw_all();
        return;
    }
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
    int next  = (wm_active + 1) % WM_MAX_WINDOWS;
    while (next != start && wm_windows[next].hidden)
        next = (next + 1) % WM_MAX_WINDOWS;
    /* If all others are hidden, next == start — stays on current (no-op). */
    if (next != wm_active)
        wm_set_active(next);
    wm_draw_all();
}

int wm_active_is_terminal(void) {
    /* Safety: if the active slot is somehow hidden, treat as terminal */
    if (wm_windows[wm_active].hidden) return 1;
    return wm_windows[wm_active].type == WM_TYPE_TERMINAL;
}

int wm_active_is_stext(void) {
    if (wm_windows[wm_active].hidden) return 0;
    return wm_windows[wm_active].type == WM_TYPE_STEXT;
}

void wm_stext_handle_key(int key_type, char ch) {
    si = &stext_instances[wm_windows[wm_active].instance];
    if (key_type == KEY_EVENT_CHAR) {
        if (ch >= 32 && ch <= 126) stext_insert_char(ch);
    } else if (key_type == KEY_EVENT_BACKSPACE) {
        stext_backspace();
    } else if (key_type == KEY_EVENT_ENTER) {
        stext_newline();
    } else if (key_type == KEY_EVENT_DELETE) {
        stext_delete_fwd();
    } else if (key_type == KEY_EVENT_LEFT  || key_type == KEY_EVENT_RIGHT ||
               key_type == KEY_EVENT_UP    || key_type == KEY_EVENT_DOWN) {
        stext_move(key_type);
    }
    wm_draw_all();
}
