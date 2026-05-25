/*
 * calc.c — GUI Calculator for SiMPLE OS
 * 
 * A draggable calculator window with clickable buttons.
 * Click buttons to input numbers and operations.
 * Press ESC or click [X] to close.
 */

#include "wm.h"

void exit(int code);
int  write(const char *buf, int len);
int  wm_create(int x, int y, int w, int h);
int  wm_destroy(int wid);
int  wm_blit(int wid, unsigned int *buf, int len);
int  wm_event(wm_event_t *ev, int max);
int  wm_setfocus(int wid);

#define WIN_X  200
#define WIN_Y  100
#define WIN_W  240
#define WIN_H  320

#define DISPLAY_H  40
#define BTN_ROWS   5
#define BTN_COLS   4
#define BTN_W      55
#define BTN_H      50
#define BTN_SPACE  5

#define SC_ESC  0x01

static unsigned int pixels[WIN_W * WIN_H];

/* Calculator state */
static int current = 0;
static int stored = 0;
static char op = 0;  /* 0=none, +,-,*,/ */

static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

/* Draw a filled rectangle */
static void draw_rect(int x, int y, int w, int h, unsigned int color) {
    for (int py = y; py < y + h && py < WIN_H; py++) {
        for (int px = x; px < x + w && px < WIN_W; px++) {
            if (px >= 0 && py >= 0)
                pixels[py * WIN_W + px] = color;
        }
    }
}

/* Draw a single character (5x7 bitmap, scaled 2x) */
static void draw_char(int x, int y, char c, unsigned int color) {
    static const unsigned char font[16][7] = {
        {0x3E,0x51,0x49,0x45,0x3E}, // 0
        {0x00,0x42,0x7F,0x40,0x00}, // 1
        {0x42,0x61,0x51,0x49,0x46}, // 2
        {0x21,0x41,0x45,0x4B,0x31}, // 3
        {0x18,0x14,0x12,0x7F,0x10}, // 4
        {0x27,0x45,0x45,0x45,0x39}, // 5
        {0x3C,0x4A,0x49,0x49,0x30}, // 6
        {0x01,0x71,0x09,0x05,0x03}, // 7
        {0x36,0x49,0x49,0x49,0x36}, // 8
        {0x06,0x49,0x49,0x29,0x1E}, // 9
        {0x7C,0x12,0x11,0x12,0x7C}, // + (index 10)
        {0x08,0x08,0x08,0x08,0x08}, // - (index 11)
        {0x22,0x14,0x08,0x14,0x22}, // * (index 12)
        {0x02,0x01,0x51,0x09,0x06}, // / (index 13)
        {0x08,0x08,0x3E,0x08,0x08}, // = (index 14)
        {0x7E,0x09,0x09,0x09,0x7E}, // C (index 15)
    };
    
    int idx = -1;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == '+') idx = 10;
    else if (c == '-') idx = 11;
    else if (c == '*') idx = 12;
    else if (c == '/') idx = 13;
    else if (c == '=') idx = 14;
    else if (c == 'C') idx = 15;
    
    if (idx < 0) return;
    
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 8; col++) {
            if (font[idx][row] & (1 << col)) {
                draw_rect(x + col * 2, y + row * 2, 2, 2, color);
            }
        }
    }
}

/* Draw a button */
static void draw_button(int col, int row, char label, unsigned int bg, unsigned int fg) {
    int bx = col * (BTN_W + BTN_SPACE) + BTN_SPACE;
    int by = DISPLAY_H + row * (BTN_H + BTN_SPACE) + BTN_SPACE;
    
    draw_rect(bx, by, BTN_W, BTN_H, bg);
    draw_char(bx + 20, by + 17, label, fg);
}

/* Draw the display */
static void draw_display(int value) {
    draw_rect(0, 0, WIN_W, DISPLAY_H, 0x202020);
    
    /* Convert number to string */
    char buf[12];
    int i = 0;
    int neg = 0;
    
    if (value < 0) {
        neg = 1;
        value = -value;
    }
    
    if (value == 0) {
        buf[i++] = '0';
    } else {
        int temp = value;
        int digits[10];
        int n = 0;
        while (temp > 0) {
            digits[n++] = temp % 10;
            temp /= 10;
        }
        if (neg) buf[i++] = '-';
        for (int j = n - 1; j >= 0; j--) {
            buf[i++] = '0' + digits[j];
        }
    }
    buf[i] = '\0';
    
    /* Draw right-aligned */
    int x = WIN_W - 20;
    for (int j = i - 1; j >= 0; j--) {
        draw_char(x - 16, 12, buf[j], 0xFFFFFF);
        x -= 16;
    }
}

/* Redraw everything */
static void redraw(void) {
    /* Clear background */
    draw_rect(0, 0, WIN_W, WIN_H, 0x1A1A1A);
    
    /* Draw display */
    draw_display(current);
    
    /* Draw buttons */
    const char layout[BTN_ROWS][BTN_COLS] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'},
        {' ', ' ', ' ', ' '}
    };
    
    for (int r = 0; r < BTN_ROWS; r++) {
        for (int c = 0; c < BTN_COLS; c++) {
            char label = layout[r][c];
            if (label != ' ') {
                unsigned int bg = 0x404040;
                if (label >= '0' && label <= '9') bg = 0x505050;
                if (label == '=') bg = 0x007ACC;
                if (label == 'C') bg = 0xCC3333;
                draw_button(c, r, label, bg, 0xFFFFFF);
            }
        }
    }
}

/* Handle button click */
static void click_button(int x, int y) {
    /* Check if in button area */
    if (y < DISPLAY_H) return;
    
    /* Find which button */
    int col = (x - BTN_SPACE) / (BTN_W + BTN_SPACE);
    int row = (y - DISPLAY_H - BTN_SPACE) / (BTN_H + BTN_SPACE);
    
    if (col < 0 || col >= BTN_COLS || row < 0 || row >= BTN_ROWS) return;
    
    const char layout[BTN_ROWS][BTN_COLS] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'C', '0', '=', '+'},
        {' ', ' ', ' ', ' '}
    };
    
    char btn = layout[row][col];
    
    if (btn >= '0' && btn <= '9') {
        /* Digit input */
        current = current * 10 + (btn - '0');
    } else if (btn == 'C') {
        /* Clear */
        current = 0;
        stored = 0;
        op = 0;
    } else if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
        /* Operator */
        stored = current;
        current = 0;
        op = btn;
    } else if (btn == '=') {
        /* Calculate */
        if (op == '+') current = stored + current;
        else if (op == '-') current = stored - current;
        else if (op == '*') current = stored * current;
        else if (op == '/') {
            if (current != 0) current = stored / current;
            else current = 0;
        }
        stored = 0;
        op = 0;
    }
}

void _start(void) {
    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) {
        print("calc: failed to create window\n");
        exit(1);
    }
    
    wm_setfocus(wid);
    
    /* Initial draw */
    redraw();
    
    if (wm_blit(wid, pixels, WIN_W * WIN_H * 4) < 0) {
        print("calc: wm_blit failed\n");
        wm_destroy(wid);
        exit(1);
    }
    
    print("calc: ready — click buttons or press ESC to exit\n");
    
    wm_event_t ev;
    while (1) {
        int r = wm_event(&ev, (int)sizeof(ev));
        
        if (r == WM_EV_KEY_DOWN && (ev.x & 0xFF) == SC_ESC) break;
        if (r == WM_EV_CLOSE) break;
        
        if (r == WM_EV_MOUSE_BTN && (ev.btn & 1)) {
            /* Left click */
            click_button(ev.x, ev.y);
            redraw();
            wm_blit(wid, pixels, WIN_W * WIN_H * 4);
        }
    }
    
    wm_destroy(wid);
    print("calc: closed\n");
    exit(0);
}