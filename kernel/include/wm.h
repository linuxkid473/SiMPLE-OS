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
 *     vga_repaint_cells).  Other windows draw directly to the framebuffer
 *     with fb_fill_rect / fb_draw_string_px.
 *   - The launcher bar is a system-level UI strip that is always rendered
 *     on top of all windows and is hit-tested before windows.
 *
 * Key bindings:
 *   Alt + Arrow  — move the currently focused window
 *   Alt + Tab    — cycle focus to the next visible window
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

/* Calculator window — fits display bar + 4×4 clickable button grid.
 * client area: 158 × 128
 * window total: (2+158+2) × (18+128+2) = 162 × 148 */
#define WM_CALC_W  162
#define WM_CALC_H  148

/* SText editor window — 44 cols × 16 visible rows at 8 px/cell.
 * text area: 44*8=352 wide, 16*8=128 tall; 4 px padding each side.
 * client area: 360 × 136
 * window total: (2+360+2) × (18+136+2) = 364 × 156 */
#define WM_STEXT_W 364
#define WM_STEXT_H 156

/* Maximum simultaneous windows on screen.
 * 3 app types × 3 instances each = 9 worst case. */
#define WM_MAX_WINDOWS    9

/* Per-type instance pool sizes.  Sum must fit in WM_MAX_WINDOWS. */
#define WM_MAX_TERM_INST  3
#define WM_MAX_CALC_INST  3
#define WM_MAX_STEXT_INST 3

/* ---- window type ---- */
typedef enum {
    WM_TYPE_TERMINAL = 0,
    WM_TYPE_CALC,
    WM_TYPE_STEXT
} wm_win_type_t;

/* ---- window descriptor ---- */
typedef struct {
    int            x, y;        /* top-left pixel on the screen           */
    int            width, height;
    const char    *title;
    wm_win_type_t  type;
    int            instance;    /* index into the per-type instance pool  */
    int            hidden;      /* 1 = not rendered, not focusable,
                                 *     not hit-tested by mouse             */
} wm_window_t;

/* ---- global state (read-only outside wm.c) ---- */
extern wm_window_t wm_windows[WM_MAX_WINDOWS];
extern int         wm_active;    /* index of the focused window */

/* ---- API ---- */

/* Initialise window slots and draw the first frame.
 * Must be called after fb_init(). */
void wm_init(int screen_w, int screen_h);

/* Redraw desktop + launcher + all visible windows + cursor.
 * Safe to call at any time; leaves vga text API anchored to the terminal. */
void wm_draw_all(void);

/* Move the active window one step in the given KEY_EVENT_* direction. */
void wm_handle_key(int key_type);

/* Cycle focus to the next visible (non-hidden) window and repaint. */
void wm_tab_switch(void);

/* Returns 1 if the currently focused window is the terminal. */
int wm_active_is_terminal(void);

/* Returns 1 if the currently focused window is the SText editor. */
int wm_active_is_stext(void);

/* Route a key event to the SText editor.
 * key_type is a KEY_EVENT_* value cast to int; ch is valid only for
 * KEY_EVENT_CHAR.  Always calls wm_draw_all() before returning. */
void wm_stext_handle_key(int key_type, char ch);

/* Feed one character (or '\b' for backspace) to the calculator.
 * Ignored unless the calculator window exists.
 * Always calls wm_draw_all() before returning. */
void wm_calc_handle_char(char c);

/*
 * Process a mouse event.  Called by mouse.c on each complete 3-byte packet.
 * x, y         — new cursor pixel position (already clamped to screen).
 * new_buttons  — current button bitmask (bit 0 = left).
 * prev_buttons — button state from the previous packet.
 */
void wm_handle_mouse(int x, int y, uint8_t new_buttons, uint8_t prev_buttons);

#endif
