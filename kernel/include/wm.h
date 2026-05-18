#ifndef SIMPLE_WM_H
#define SIMPLE_WM_H

#include "types.h"

/*
 * Minimal TempleOS-inspired pseudo window manager.
 * One global window; no compositor, no z-order, no mouse yet.
 * Arrow keys move the window; normal typing still goes to the shell.
 *
 * Geometry constants are chosen so the client area is exactly
 * 80 cols × 49 rows on an 800×600 screen, matching console.c's
 * CONSOLE_LINE_WIDTH = 80 assumption.
 */

#define WM_BORDER      2    /* px — thin flat border on all sides          */
#define WM_TITLEBAR_H  18   /* px — includes the WM_BORDER top stripe      */
#define WM_MOVE_STEP   8    /* px per arrow key press (one char cell width) */

/* 2-border + 80*8 client + 2-border = 644; 18-titlebar + 49*8 client + 2-border = 412 */
#define WM_WIN_W  644
#define WM_WIN_H  412

typedef struct {
    int x, y;           /* top-left pixel on screen */
    int width, height;  /* outer dimensions in pixels */
    const char *title;
} wm_window_t;

extern wm_window_t wm_win;

/* Call once after fb_init().  Draws the initial frame. */
void wm_init(int screen_w, int screen_h);

/* Draw desktop background + window chrome.  Does NOT draw text content. */
void wm_draw_frame(void);

/*
 * Handle an arrow key event.  key_type is one of the KEY_EVENT_* values
 * from keyboard.h (UP/DOWN/LEFT/RIGHT).  Moves the window, clamps it to
 * the screen, and repaints everything.
 */
void wm_handle_key(int key_type);

#endif
