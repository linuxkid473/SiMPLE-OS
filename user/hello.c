/*
 * wm.c — SiMPLE-OS ring-3 window manager demo
 *
 * Compile:
 *   gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
 *       -Wl,-T,user/linker.ld -Wl,-N \
 *       -o user/wm.elf user/wm.c user/libc.c
 *
 * Then add to Makefile and mcopy as ::wm.elf
 * Run from shell: run wm.elf
 */

#include "wm.h"

/* ------------------------------------------------------------------ */
/* Minimal libc.h declarations (matches what libc.c already exports)  */
/* ------------------------------------------------------------------ */
int   write(const char *buf, int len);
void  exit(void);
int   wm_create(int x, int y, int w, int h);
int   wm_destroy(int wid);
int   wm_blit(int wid, unsigned int *buf, int len);
int   wm_move(int wid, int x, int y);
int   wm_event(wm_event_t *ev, int max);
int   wm_flush(int wid);
int   wm_setfocus(int wid);

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */
#define SCREEN_W   800
#define SCREEN_H   600

#define WIN_W      200
#define WIN_H      120
#define WIN_X       50
#define WIN_Y       50

#define WIN2_W     160
#define WIN2_H     100
#define WIN2_X     300
#define WIN2_Y     200

/* pixel buffer for each window (static — lives in BSS at 0x300000+) */
static unsigned int buf1[WIN_W  * WIN_H ];
static unsigned int buf2[WIN2_W * WIN2_H];

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void fill(unsigned int *buf, int w, int h, unsigned int color)
{
    int n = w * h;
    for (int i = 0; i < n; i++) buf[i] = color;
}

/* draw a 1-px border inside the buffer */
static void border(unsigned int *buf, int w, int h, unsigned int color)
{
    for (int x = 0; x < w; x++) {
        buf[x]               = color;   /* top row    */
        buf[(h-1)*w + x]     = color;   /* bottom row */
    }
    for (int y = 0; y < h; y++) {
        buf[y*w]             = color;   /* left col   */
        buf[y*w + (w-1)]     = color;   /* right col  */
    }
}

/* draw a filled rect inside the buffer */
static void rect(unsigned int *buf, int bw,
                 int rx, int ry, int rw, int rh,
                 unsigned int color)
{
    for (int y = ry; y < ry + rh; y++)
        for (int x = rx; x < rx + rw; x++)
            buf[y * bw + x] = color;
}

static void print(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

static void print_int(int n)
{
    if (n < 0) { write("-", 1); n = -n; }
    char tmp[12];
    int i = 11;
    tmp[i] = 0;
    if (n == 0) { write("0", 1); return; }
    while (n && i > 0) { tmp[--i] = '0' + (n % 10); n /= 10; }
    print(tmp + i);
}

/* ------------------------------------------------------------------ */
/* _start                                                              */
/* ------------------------------------------------------------------ */
void _start(void)
{
    print("[wm] starting ring-3 WM demo\n");

    /* ---- window 1: blue with white border ------------------------- */
    fill(buf1, WIN_W, WIN_H, 0xFF1155AA);
    border(buf1, WIN_W, WIN_H, 0xFFFFFFFF);
    /* small red square in corner as a visual marker */
    rect(buf1, WIN_W, 4, 4, 20, 20, 0xFFDD3333);

    int w1 = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (w1 < 0) {
        print("[wm] ERROR: wm_create w1 failed: ");
        print_int(w1);
        print("\n");
        exit();
    }
    print("[wm] window 1 created, wid=");
    print_int(w1);
    print("\n");

    wm_setfocus(w1);
    wm_blit(w1, buf1, WIN_W * WIN_H * 4);
    wm_flush(w1);
    print("[wm] window 1 blitted (blue, white border, red corner)\n");

    /* ---- window 2: green with yellow border ----------------------- */
    fill(buf2, WIN2_W, WIN2_H, 0xFF226622);
    border(buf2, WIN2_W, WIN2_H, 0xFFFFDD00);
    /* cyan stripe across the middle */
    rect(buf2, WIN2_W, 0, WIN2_H/2 - 4, WIN2_W, 8, 0xFF00CCCC);

    int w2 = wm_create(WIN2_X, WIN2_Y, WIN2_W, WIN2_H);
    if (w2 < 0) {
        print("[wm] ERROR: wm_create w2 failed: ");
        print_int(w2);
        print("\n");
        /* still continue with just one window */
    } else {
        print("[wm] window 2 created, wid=");
        print_int(w2);
        print("\n");
        wm_blit(w2, buf2, WIN2_W * WIN2_H * 4);
        wm_flush(w2);
        print("[wm] window 2 blitted (green, yellow border, cyan stripe)\n");
    }

    /* ---- event loop ----------------------------------------------- */
    print("[wm] event loop: move mouse to drag w1, press any key to exit\n");

    wm_event_t ev;
    int running = 1;
    int mx = WIN_X, my = WIN_Y;   /* track w1 position */

    while (running) {
        int type = wm_event(&ev, sizeof(wm_event_t));

        if (type == WM_EV_KEY_DOWN) {
            print("[wm] key down scancode=");
            print_int((int)(unsigned short)ev.x);
            print(" — exiting\n");
            running = 0;
        }
        else if (type == WM_EV_MOUSE_MOV) {
            int nx = (int)(short)ev.x - WIN_W / 2;
            int ny = (int)(short)ev.y - WIN_H / 2;

            /* clamp to screen */
            if (nx < 0)                  nx = 0;
            if (ny < 0)                  ny = 0;
            if (nx + WIN_W > SCREEN_W)   nx = SCREEN_W - WIN_W;
            if (ny + WIN_H > SCREEN_H)   ny = SCREEN_H - WIN_H;

            if (nx != mx || ny != my) {
                mx = nx;
                my = ny;
                wm_move(w1, mx, my);
                /* re-blit because move only re-draws old backing store */
                wm_blit(w1, buf1, WIN_W * WIN_H * 4);
                wm_flush(w1);
            }
        }
        else if (type == WM_EV_MOUSE_BTN) {
            /* right-click cycles window 1 colour */
            if (ev.btn & 0x02) {
                static unsigned int colors[] = {
                    0xFF1155AA,   /* blue   */
                    0xFFAA2211,   /* red    */
                    0xFF117733,   /* green  */
                    0xFF885500,   /* orange */
                };
                static int ci = 0;
                ci = (ci + 1) & 3;
                fill(buf1, WIN_W, WIN_H, colors[ci]);
                border(buf1, WIN_W, WIN_H, 0xFFFFFFFF);
                rect(buf1, WIN_W, 4, 4, 20, 20, 0xFFDD3333);
                wm_blit(w1, buf1, WIN_W * WIN_H * 4);
                wm_flush(w1);
                print("[wm] right-click: changed w1 colour\n");
            }
        }
        /* type == 0 → queue empty, tight-loop is fine for a demo */
    }

    /* ---- cleanup -------------------------------------------------- */
    wm_destroy(w1);
    if (w2 >= 0) wm_destroy(w2);
    print("[wm] windows destroyed, done\n");
    exit();
}