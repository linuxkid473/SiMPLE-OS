/*
 * wmtest.c — ring-3 WM window demo.
 *
 * Creates a 280×180 content window with a colour gradient.
 * The kernel WM provides a title bar you can drag with the mouse
 * and a close [X] button.  Press ESC or click [X] to exit.
 */

#include "wm.h"

void exit(int code);
int  write(const char *buf, int len);
int  wm_create(int x, int y, int w, int h);
int  wm_destroy(int wid);
int  wm_blit(int wid, unsigned int *buf, int len);
int  wm_event(wm_event_t *ev, int max);
int  wm_setfocus(int wid);

#define WIN_X  160
#define WIN_Y   80
#define WIN_W  280
#define WIN_H  180

/* Pixel buffer lives in BSS — 280*180*4 = ~196 KB, fits in user space */
static unsigned int pixels[WIN_W * WIN_H];

static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

static void fill_gradient(void) {
    for (int py = 0; py < WIN_H; py++) {
        for (int px = 0; px < WIN_W; px++) {
            unsigned int r = (unsigned int)(px * 220) / WIN_W + 20;
            unsigned int g = (unsigned int)(py * 200) / WIN_H + 30;
            unsigned int b = 180 - (unsigned int)(px * 80) / WIN_W;
            pixels[py * WIN_W + px] = (r << 16) | (g << 8) | b;
        }
    }
}

void _start(void) {
    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) {
        print("wmtest: wm_create failed\n");
        exit(1);
    }

    wm_setfocus(wid);

    fill_gradient();

    if (wm_blit(wid, pixels, WIN_W * WIN_H * 4) < 0) {
        print("wmtest: wm_blit failed\n");
        wm_destroy(wid);
        exit(1);
    }

    print("wmtest: window open — drag by title bar, ESC or [X] to close\n");

    wm_event_t ev;
    while (1) {
        int r = wm_event(&ev, (int)sizeof(ev));
        if (r == WM_EV_KEY_DOWN && (ev.x & 0xFF) == SC_ESC) break;
        if (r == WM_EV_CLOSE) break;
    }

    wm_destroy(wid);
    print("wmtest: closed\n");
    exit(0);
}
