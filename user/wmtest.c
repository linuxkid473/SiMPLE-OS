/*
 * wmtest.c — ring-3 WM syscall test.
 *
 * 1. Create a 200×100 window at (50, 50).
 * 2. Fill a pixel buffer with solid blue and display it.
 * 3. Poll wm_event in a loop:
 *    - key_down  → exit
 *    - mouse_move → drag the window to the cursor position
 * 4. Destroy window and exit.
 */

#include "wm.h"

/* Forward declarations for libc wrappers */
void exit(int code);
int  write(const char *buf, int len);
int  wm_create(int x, int y, int w, int h);
int  wm_destroy(int wid);
int  wm_blit(int wid, unsigned int *buf, int len);
int  wm_move(int wid, int x, int y);
int  wm_event(wm_event_t *ev, int max);
int  wm_flush(int wid);
int  wm_setfocus(int wid);

#define WIN_X  50
#define WIN_Y  50
#define WIN_W 200
#define WIN_H 100

static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

/* Pixel buffer lives in BSS — 200*100*4 = 80 KB, well within user space */
static unsigned int pixels[WIN_W * WIN_H];

void _start(void) {
    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) {
        print("wmtest: wm_create failed\n");
        exit(1);
    }

    wm_setfocus(wid);

    /* Fill with solid #005599FF blue */
    int i;
    for (i = 0; i < WIN_W * WIN_H; i++)
        pixels[i] = 0x005599FFu;

    if (wm_blit(wid, pixels, WIN_W * WIN_H * 4) < 0) {
        print("wmtest: wm_blit failed\n");
        wm_destroy(wid);
        exit(1);
    }
    wm_flush(wid);

    print("wmtest: window up — press any key to exit, move mouse to drag\n");

    /* Event loop */
    wm_event_t ev;
    while (1) {
        int r = wm_event(&ev, (int)sizeof(ev));
        if (r == WM_EV_KEY_DOWN) break;
        if (r == WM_EV_MOUSE_MOV) {
            /* Drag the window so its top-left follows the cursor */
            wm_move(wid, (int)ev.x, (int)ev.y);
        }
    }

    wm_destroy(wid);
    print("wmtest: done\n");
    exit(0);
}
