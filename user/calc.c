/*
 * calc.c  —  GUI integer calculator for SiMPLE OS
 *
 * Creates a 240×320 pixel-buffer window via WM syscalls.
 * Buttons are hit-tested against client-relative mouse coordinates
 * (the kernel subtracts the window origin before pushing events).
 * Press ESC or click the [X] title-bar button to exit.
 */

#include "wm.h"

/* ── libc stubs ── */
void exit(int code);
int  write(const char *buf, int len);
int  wm_create(int x, int y, int w, int h);
int  wm_destroy(int wid);
int  wm_blit(int wid, unsigned int *buf, int len);
int  wm_event(wm_event_t *ev, int max);
int  wm_setfocus(int wid);

/* ── window geometry ── */
#define WIN_X   190
#define WIN_Y    60
#define WIN_W   240
#define WIN_H   320

/* ── layout ── */
#define DISP_H   70      /* display strip height                              */
#define BTN_COLS  4
#define BTN_ROWS  4
#define PAD       5      /* gap between buttons and at edges                  */
/* BTN_W: 4*BTN_W + 5*PAD = 240  →  BTN_W = (240-25)/4 = 53  (leaves 3px)  */
/* BTN_H: 4*BTN_H + 5*PAD = 250  →  BTN_H = (250-25)/4 = 56  (leaves 1px)  */
#define BTN_W    53
#define BTN_H    56

/* ── colours (0x00RRGGBB) ── */
#define COL_BG       0x1A1B2E
#define COL_DISP_BG  0x0B0C18
#define COL_DISP_TXT 0x80FFAA
#define COL_DISP_OP  0x4488CC
#define COL_NUM      0x2C3461
#define COL_NUM_HL   0x3C4481
#define COL_OP       0x1A5C8C
#define COL_EQ       0x1A7A48
#define COL_CLR      0x8C1A1A
#define COL_BTN_TXT  0xE0E4FF
#define COL_EDGE_LT  0x5860A0
#define COL_EDGE_DK  0x0E0F1C

/* ── pixel buffer (240×320×4 = 300 KB in BSS) ── */
static unsigned int pixels[WIN_W * WIN_H];

/* ── 8×8 bitmap font (MSB = leftmost pixel, row 0 = top) ── */
static const unsigned char FONT[128][8] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['0'] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2'] = {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    ['3'] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['4'] = {0x06,0x0E,0x1E,0x66,0x7F,0x06,0x06,0x00},
    ['5'] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    ['6'] = {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    ['7'] = {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00},
    ['8'] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    ['9'] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    ['+'] = {0x00,0x18,0x18,0x7E,0x7E,0x18,0x18,0x00},
    ['-'] = {0x00,0x00,0x00,0x7E,0x7E,0x00,0x00,0x00},
    ['*'] = {0x00,0x36,0x1C,0x7F,0x1C,0x36,0x00,0x00},
    ['/'] = {0x00,0x06,0x0C,0x18,0x30,0x60,0x00,0x00},
    ['='] = {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00},
    ['C'] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    ['E'] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    ['r'] = {0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x00},
    ['R'] = {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    ['o'] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
};

/* ── button descriptor ── */
typedef struct { char label[3]; char act; unsigned int col; } btn_t;

/*
 * 4×4 grid:
 *   Row 0:  C   /   *   -
 *   Row 1:  7   8   9   +
 *   Row 2:  4   5   6   =
 *   Row 3:  1   2   3   0
 */
static const btn_t BTNS[BTN_ROWS][BTN_COLS] = {
    { {"C",'C',COL_CLR}, {"/",'/',COL_OP}, {"*",'*',COL_OP}, {"-",'-',COL_OP} },
    { {"7",'7',COL_NUM}, {"8",'8',COL_NUM},{"9",'9',COL_NUM},{"+",'+',COL_OP} },
    { {"4",'4',COL_NUM}, {"5",'5',COL_NUM},{"6",'6',COL_NUM},{"=",'=',COL_EQ} },
    { {"1",'1',COL_NUM}, {"2",'2',COL_NUM},{"3",'3',COL_NUM},{"0",'0',COL_NUM}},
};

/* ── calculator state ── */
static char disp[14];   /* display string (up to 13 chars + NUL)  */
static long accum;      /* left-hand operand                       */
static char pend_op;    /* pending operator or 0                   */
static int  fresh;      /* 1 = next digit clears display           */
static int  err;        /* 1 = divide-by-zero                      */

/* ── helpers ── */
static int slen(const char *s) { int n=0; while(s[n]) n++; return n; }

static void ltoa(long n, char *buf) {
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[14]; int i=0, neg=0;
    if (n < 0) { neg=1; n=-n; }
    while (n > 0) { tmp[i++] = '0' + (int)(n % 10); n /= 10; }
    if (neg) tmp[i++] = '-';
    int j=0; while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static long atol_local(const char *s) {
    long r=0; int neg=0, i=0;
    if (s[0]=='-') { neg=1; i=1; }
    for (; s[i]>='0' && s[i]<='9'; i++) r = r*10 + (s[i]-'0');
    return neg ? -r : r;
}

/* ── pixel-buffer drawing ── */
static void pset(int x, int y, unsigned int c) {
    if ((unsigned)x < WIN_W && (unsigned)y < WIN_H)
        pixels[y * WIN_W + x] = c;
}

static void fill(int x, int y, int w, int h, unsigned int c) {
    for (int dy=0; dy<h; dy++)
        for (int dx=0; dx<w; dx++)
            pset(x+dx, y+dy, c);
}

/* Draw one 8×8 glyph at pixel scale `sc`. */
static void draw_glyph(int x, int y, unsigned char ch, unsigned int fg, int sc) {
    if ((unsigned)ch >= 128) return;
    const unsigned char *rows = FONT[ch];
    for (int row=0; row<8; row++) {
        unsigned char bits = rows[row];
        for (int col=0; col<8; col++) {
            if (bits & (0x80u >> col))
                fill(x + col*sc, y + row*sc, sc, sc, fg);
        }
    }
}

static void draw_str(int x, int y, const char *s, unsigned int fg, int sc) {
    for (; *s; s++, x += 8*sc)
        draw_glyph(x, y, (unsigned char)*s, fg, sc);
}

/* Draw right-aligned: rightmost pixel edge at x_end. */
static void draw_str_r(int x_end, int y, const char *s, unsigned int fg, int sc) {
    draw_str(x_end - slen(s)*8*sc, y, s, fg, sc);
}

/* ── rendering ── */
static void draw_display(void) {
    fill(0, 0, WIN_W, DISP_H, COL_DISP_BG);

    /* Separator line */
    fill(0, DISP_H-3, WIN_W, 3, 0x252545);

    /* Pending operator indicator — top-left, small */
    if (pend_op && !err) {
        char opstr[2] = {pend_op, '\0'};
        draw_str(6, 6, opstr, COL_DISP_OP, 1);
    }

    /* Main number — right-aligned, scale 3 for ≤9 chars, scale 2 for longer */
    const char *s = err ? "Err" : disp;
    unsigned int col = err ? 0xFF4444u : COL_DISP_TXT;
    int sc = (slen(s) <= 9) ? 3 : 2;
    int ty = (DISP_H - 8*sc) / 2 - 1;
    draw_str_r(WIN_W - 8, ty, s, col, sc);
}

static void draw_button(int r, int c) {
    const btn_t *b = &BTNS[r][c];
    int bx = PAD + c * (BTN_W + PAD);
    int by = DISP_H + PAD + r * (BTN_H + PAD);

    /* Body */
    fill(bx, by, BTN_W, BTN_H, b->col);

    /* Raised-edge highlight (top/left brighter, bottom/right darker) */
    fill(bx,           by,           BTN_W, 1, COL_EDGE_LT);
    fill(bx,           by,           1, BTN_H, COL_EDGE_LT);
    fill(bx,           by+BTN_H-1,  BTN_W, 1, COL_EDGE_DK);
    fill(bx+BTN_W-1,  by,           1, BTN_H, COL_EDGE_DK);

    /* Label centred at scale 2 */
    int tw = slen(b->label) * 8 * 2;
    int th = 8 * 2;
    int tx = bx + (BTN_W - tw) / 2;
    int ty = by + (BTN_H - th) / 2;
    draw_str(tx, ty, b->label, COL_BTN_TXT, 2);
}

static void redraw(void) {
    fill(0, 0, WIN_W, WIN_H, COL_BG);
    draw_display();
    for (int r=0; r<BTN_ROWS; r++)
        for (int c=0; c<BTN_COLS; c++)
            draw_button(r, c);
}

/* ── calculator logic ── */
static void calc_reset(void) {
    disp[0]='0'; disp[1]='\0';
    accum=0; pend_op=0; fresh=1; err=0;
}

static void do_op(char op, long rhs) {
    switch (op) {
    case '+': accum += rhs; break;
    case '-': accum -= rhs; break;
    case '*': accum *= rhs; break;
    case '/':
        if (rhs == 0) { err = 1; return; }
        accum /= rhs;
        break;
    }
    ltoa(accum, disp);
}

static void calc_press(char act) {
    if (err && act != 'C') return;

    if (act >= '0' && act <= '9') {
        if (fresh) { disp[0]=act; disp[1]='\0'; fresh=0; }
        else {
            int len = slen(disp);
            if (len < 9) { disp[len]=act; disp[len+1]='\0'; }
        }
    } else if (act=='+' || act=='-' || act=='*' || act=='/') {
        if (!fresh) {
            long rhs = atol_local(disp);
            if (pend_op) do_op(pend_op, rhs);
            else         accum = rhs;
            if (!err) fresh = 1;
        }
        if (!err) { pend_op=act; fresh=1; }
    } else if (act == '=') {
        if (pend_op && !fresh) {
            long rhs = atol_local(disp);
            do_op(pend_op, rhs);
            pend_op=0; fresh=1;
        }
    } else if (act == 'C') {
        calc_reset();
    }
}

/* ── hit test ── */
static int hit_btn(int mx, int my, int *pr, int *pc) {
    for (int r=0; r<BTN_ROWS; r++) {
        for (int c=0; c<BTN_COLS; c++) {
            int bx = PAD + c*(BTN_W+PAD);
            int by = DISP_H + PAD + r*(BTN_H+PAD);
            if (mx>=bx && mx<bx+BTN_W && my>=by && my<by+BTN_H) {
                *pr=r; *pc=c; return 1;
            }
        }
    }
    return 0;
}

/* ── entry point ── */
void _start(void) {
    calc_reset();

    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) { exit(1); }

    wm_setfocus(wid);
    redraw();
    wm_blit(wid, pixels, WIN_W * WIN_H * 4);

    wm_event_t ev;
    for (;;) {
        int r = wm_event(&ev, (int)sizeof(ev));
        if (r <= 0) continue;

        if (r == WM_EV_CLOSE) break;
        if (r == WM_EV_KEY_DOWN && (ev.x & 0xFF) == SC_ESC) break;

        /* Left-button press in client area → hit test */
        if (r == WM_EV_MOUSE_BTN && (ev.btn & 1)) {
            int row, col;
            if (hit_btn((int)ev.x, (int)ev.y, &row, &col)) {
                char act = BTNS[row][col].act;
                if (act) {
                    calc_press(act);
                    redraw();
                    wm_blit(wid, pixels, WIN_W * WIN_H * 4);
                }
            }
        }
    }

    wm_destroy(wid);
    exit(0);
}
