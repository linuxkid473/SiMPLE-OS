#include "wm.h"
#include "vga.h"
#include "keyboard.h"

wm_window_t wm_win;

static int scr_w;
static int scr_h;

/* Flat TempleOS-inspired palette — raw 32-bit RGB */
#define COL_DESKTOP  0x00001A   /* very dark navy desktop */
#define COL_BORDER   0x888888   /* mid-grey single-pixel-style border */
#define COL_TITLEBG  0x0000AA   /* classic blue title bar */
#define COL_TITLEFG  0xFFFFFF   /* white title text */
#define COL_CLIENTBG 0x000000   /* black terminal background */

/*
 * Tell vga.c where the client area starts and how big it is.
 * Called on init and every time the window moves.
 */
static void sync_client_origin(void) {
    int cx   = wm_win.x + WM_BORDER;
    int cy   = wm_win.y + WM_TITLEBAR_H;
    int cw   = wm_win.width  - 2 * WM_BORDER;
    int ch   = wm_win.height - WM_TITLEBAR_H - WM_BORDER;
    vga_set_client(cx, cy, (uint32_t)(cw / 8), (uint32_t)(ch / 8));
}

void wm_init(int sw, int sh) {
    scr_w = sw;
    scr_h = sh;

    wm_win.width  = WM_WIN_W;
    wm_win.height = WM_WIN_H;
    wm_win.x      = (sw - WM_WIN_W) / 2;
    wm_win.y      = (sh - WM_WIN_H) / 2;
    wm_win.title  = "Terminal";

    sync_client_origin();
    wm_draw_frame();
}

void wm_draw_frame(void) {
    int wx = wm_win.x,  wy = wm_win.y;
    int ww = wm_win.width, wh = wm_win.height;

    /* 1. Fill the entire screen with the desktop colour first so
     *    old window chrome at a previous position gets erased. */
    fb_fill_rect(0, 0, scr_w, scr_h, COL_DESKTOP);

    /* 2. Thin border — four rects forming a frame */
    fb_fill_rect(wx,            wy,                ww,         WM_BORDER, COL_BORDER); /* top    */
    fb_fill_rect(wx,            wy + wh - WM_BORDER, ww,       WM_BORDER, COL_BORDER); /* bottom */
    fb_fill_rect(wx,            wy,                WM_BORDER,  wh,        COL_BORDER); /* left   */
    fb_fill_rect(wx + ww - WM_BORDER, wy,          WM_BORDER,  wh,        COL_BORDER); /* right  */

    /* 3. Title bar (sits between top border and client area) */
    fb_fill_rect(wx + WM_BORDER,
                 wy + WM_BORDER,
                 ww - 2 * WM_BORDER,
                 WM_TITLEBAR_H - WM_BORDER,
                 COL_TITLEBG);

    /* 4. Title text — small left-aligned, vertically centred in bar */
    fb_draw_string_px(wx + WM_BORDER + 4,
                      wy + WM_BORDER + 3,
                      wm_win.title,
                      COL_TITLEFG, COL_TITLEBG);

    /* 5. Client area background — text rendering will draw on top of this */
    fb_fill_rect(wx + WM_BORDER,
                 wy + WM_TITLEBAR_H,
                 ww - 2 * WM_BORDER,
                 wh - WM_TITLEBAR_H - WM_BORDER,
                 COL_CLIENTBG);
}

void wm_handle_key(int key_type) {
    int dx = 0, dy = 0;

    if      (key_type == KEY_EVENT_LEFT)  dx = -WM_MOVE_STEP;
    else if (key_type == KEY_EVENT_RIGHT) dx =  WM_MOVE_STEP;
    else if (key_type == KEY_EVENT_UP)    dy = -WM_MOVE_STEP;
    else if (key_type == KEY_EVENT_DOWN)  dy =  WM_MOVE_STEP;

    wm_win.x += dx;
    wm_win.y += dy;

    /* Clamp so the window stays fully on screen */
    if (wm_win.x < 0)                           wm_win.x = 0;
    if (wm_win.y < 0)                           wm_win.y = 0;
    if (wm_win.x + wm_win.width  > scr_w)       wm_win.x = scr_w - wm_win.width;
    if (wm_win.y + wm_win.height > scr_h)       wm_win.y = scr_h - wm_win.height;

    sync_client_origin();   /* update draw offset in vga.c */
    wm_draw_frame();        /* erase old chrome, draw new chrome + cleared client */
    vga_repaint_cells();    /* replay cell buffer at the new pixel offset */
}
