#ifndef SIMPLE_WM_H
#define SIMPLE_WM_H

#include "types.h"

/*
 * Minimal TempleOS-style pseudo window manager.
 *
 * Design constraints:
 *   - Fixed array of windows; no dynamic allocation, no linked lists.
 *   - No compositor: windows are repainted in z-order (inactive first,
 *     active last) so the active window always appears on top.
 *   - No real multitasking: the shell event loop does all routing.
 *   - Terminal uses the existing vga cell-buffer path (vga_set_client +
 *     vga_repaint_cells).  The calculator draws directly to the
 *     framebuffer with fb_fill_rect / fb_draw_string_px.
 *
 * Key bindings:
 *   Alt + Arrow  — move the currently focused window
 *   Alt + Tab    — cycle focus to the next window
 *   (normal keys route to whichever app owns the focused window)
 */

/* ---- geometry constants ---- */
#define WM_BORDER      2     /* px — flat border on all four sides       */
#define WM_TITLEBAR_H  18    /* px — includes the top WM_BORDER stripe   */
#define WM_MOVE_STEP   8     /* px per Alt+Arrow press (one cell width)  */

/* Terminal window: sized for 80 cols × 49 rows at 8 px/cell.
 * 2 + 80*8 + 2 = 644   18 + 49*8 + 2 = 412 */
#define WM_TERM_W  644
#define WM_TERM_H  412

/* Calculator window */
#define WM_CALC_W  200
#define WM_CALC_H  100

#define WM_MAX_WINDOWS 3     /* upper bound on the fixed window array    */

/* ---- window type ---- */
typedef enum {
    WM_TYPE_TERMINAL = 0,
    WM_TYPE_CALC
} wm_win_type_t;

/* ---- window descriptor ---- */
typedef struct {
    int            x, y;       /* top-left pixel on the screen     */
    int            width, height;
    const char    *title;
    wm_win_type_t  type;
} wm_window_t;

/* ---- global state (read-only outside wm.c) ---- */
extern wm_window_t wm_windows[WM_MAX_WINDOWS];
extern int         wm_active;        /* index of the focused window  */
extern int         wm_window_count;

/* ---- API ---- */

/* Initialise both windows and draw the first frame.
 * Must be called after fb_init(). */
void wm_init(int screen_w, int screen_h);

/* Redraw desktop + all windows (inactive first, active last).
 * Safe to call any time; leaves vga text API anchored to the terminal. */
void wm_draw_all(void);

/* Move the active window one step in the given KEY_EVENT_* direction. */
void wm_handle_key(int key_type);

/* Cycle focus to the next window and repaint. */
void wm_tab_switch(void);

/* Returns 1 if the currently focused window is the terminal. */
int wm_active_is_terminal(void);

/* Feed one character (or '\b' for backspace) to the calculator.
 * Ignored unless the calculator window exists. */
void wm_calc_handle_char(char c);

#endif
