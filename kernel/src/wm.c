#include "wm.h"
#include "vga.h"
#include "keyboard.h"
#include "mouse.h"
#include "string.h"
#include "io.h"
#include "kmalloc.h"
#include "fat16.h"
#include "elf.h"
#include "paging.h"
#include "process.h"
#include "serial.h"
#include "kapp.h"

/* Wallpaper pixel data (400x300, nearest-neighbour scaled to screen at draw time).
 * Defined in wallpaper.c, compiled as a separate translation unit. */
#define WP_W 400
#define WP_H 300
extern const uint32_t wm_wallpaper[WP_W * WP_H];

/* ================================================================
 * Pixel buffer pool — dedicated 2 MB region for window backing stores.
 *
 * Lives at 0x700000–0x8FFFFF: inside the always-present supervisor range
 * (PDE[1] covers 0x500000–0x7FFFFF; PDE[2] is a 4 MB PSE page covering
 * 0x800000–0xBFFFFF).  This range is never used by kmalloc, the physical
 * page pool (0x900000), or process slot memory (0xA00000+).
 *
 * ALLOCATOR: tracked free-list (first-fit) so individual buffers can be
 * reclaimed when a window is destroyed.  This prevents pool exhaustion
 * across launch/close/relaunch cycles within a single session.
 * ================================================================ */
#define PIXBUF_POOL_BASE  0x700000U
#define PIXBUF_POOL_LIMIT 0x900000U   /* 2 MB */

#define PIXBUF_MAX_BLKS   WM_MAX_WINDOWS

typedef struct {
    uint32_t base;  /* absolute address in pool */
    uint32_t size;  /* bytes, 16-byte aligned   */
    int      live;  /* 1 = currently in use     */
} pixbuf_blk_t;

static pixbuf_blk_t pixbuf_blks[PIXBUF_MAX_BLKS];
static int          pixbuf_nblks    = 0;
static uint32_t     pixbuf_pool_ptr = PIXBUF_POOL_BASE;

static uint32_t *pixbuf_alloc(uint32_t bytes)
{
    bytes = (bytes + 15u) & ~15u;

    /* First-fit: reuse a freed block large enough */
    for (int i = 0; i < pixbuf_nblks; i++) {
        if (!pixbuf_blks[i].live && pixbuf_blks[i].size >= bytes) {
            pixbuf_blks[i].live = 1;
            return (uint32_t *)pixbuf_blks[i].base;
        }
    }

    /* Bump-allocate a new block from the pool */
    if (pixbuf_pool_ptr + bytes > PIXBUF_POOL_LIMIT) {
        serial_write(COM1, "[pixbuf] EXHAUST ptr=");
        serial_write_hex(COM1, pixbuf_pool_ptr);
        serial_write(COM1, " need=");
        serial_write_hex(COM1, bytes);
        serial_write(COM1, " pool_used=");
        serial_write_hex(COM1, pixbuf_pool_ptr - PIXBUF_POOL_BASE);
        serial_write(COM1, "/");
        serial_write_hex(COM1, PIXBUF_POOL_LIMIT - PIXBUF_POOL_BASE);
        serial_write(COM1, "\n");
        return (uint32_t *)0;
    }
    if (pixbuf_nblks >= PIXBUF_MAX_BLKS) {
        serial_write(COM1, "[pixbuf] REGISTRY FULL nblks=");
        serial_write_dec(COM1, (uint32_t)pixbuf_nblks);
        serial_write(COM1, "\n");
        return (uint32_t *)0;
    }

    uint32_t *p = (uint32_t *)pixbuf_pool_ptr;
    pixbuf_blks[pixbuf_nblks].base = pixbuf_pool_ptr;
    pixbuf_blks[pixbuf_nblks].size = bytes;
    pixbuf_blks[pixbuf_nblks].live = 1;
    pixbuf_nblks++;
    pixbuf_pool_ptr += bytes;
    return p;
}

/* Return a USER pixel buffer to the pool.
 * If the freed block is the topmost allocation, the bump pointer retreats,
 * making that space available for future bump allocations as well. */
static void pixbuf_free(uint32_t *buf)
{
    if (!buf) return;
    uint32_t addr = (uint32_t)buf;

    for (int i = 0; i < pixbuf_nblks; i++) {
        if (pixbuf_blks[i].base == addr && pixbuf_blks[i].live) {
            pixbuf_blks[i].live = 0;

            /* Compact trailing free blocks: retreat the bump pointer */
            while (pixbuf_nblks > 0 && !pixbuf_blks[pixbuf_nblks - 1].live) {
                pixbuf_pool_ptr -= pixbuf_blks[pixbuf_nblks - 1].size;
                pixbuf_nblks--;
            }
            return;
        }
    }
}

static void pixbuf_reset(void) {
    pixbuf_pool_ptr = PIXBUF_POOL_BASE;
    pixbuf_nblks = 0;
    for (int i = 0; i < PIXBUF_MAX_BLKS; i++) {
        pixbuf_blks[i].base = 0;
        pixbuf_blks[i].size = 0;
        pixbuf_blks[i].live = 0;
    }
    serial_write(COM1, "[pixbuf] pool reset\n");
}

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
/* drag_mode: 0=none, 1=move (title bar), 2=resize bottom-right, 3=resize bottom-left */
static int drag_active   = 0;
static int drag_mode     = 0;
static int drag_win_idx  = -1;
static int drag_off_x    = 0;   /* cursor offset from window origin / corner */
static int drag_off_y    = 0;
static int drag_right_edge = 0; /* fixed right edge for bottom-left resize   */

/* Size of the corner resize handles (pixels). */
#define RESIZE_HANDLE  10
/* Minimum window dimensions. */
#define WIN_MIN_W      80
#define WIN_MIN_H      (WM_TITLEBAR_H + 40)

/* ================================================================
 * Per-process event queues.
 * Each process slot gets its own circular buffer so events from
 * one app never bleed into another app's queue.
 * slot_eq[0] is also used for the blocking exec_elf() path.
 * ================================================================ */
#define UW_EQ_SIZE  32
static wm_event_t slot_eq     [MAX_PROCS][UW_EQ_SIZE];
static uint8_t    slot_eq_head[MAX_PROCS];
static uint8_t    slot_eq_tail[MAX_PROCS];

/* Push event e into slot s's queue (drops silently if full). */
static void wm_push_to_slot(int s, wm_event_t e) {
    if (s < 0 || s >= MAX_PROCS) return;
    uint8_t next = (uint8_t)((slot_eq_tail[s] + 1u) % UW_EQ_SIZE);
    if (next != slot_eq_head[s]) {
        slot_eq[s][slot_eq_tail[s]] = e;
        slot_eq_tail[s] = next;
    }
}

/* Flush the queue for slot s (e.g. after window cleanup). */
static void wm_flush_slot_queue(int s) {
    if (s >= 0 && s < MAX_PROCS) {
        slot_eq_head[s] = 0;
        slot_eq_tail[s] = 0;
    }
}

/* ================================================================
 * Launcher bar state
 *
 * A glass "APPS" button sits in the top-left corner.  Clicking it
 * slides open a full glass panel with categorised app entries.
 * ================================================================ */
/* launcher_open: 0=closed  1=main menu  2=ELF browser */
static int launcher_open   = 0;
static int launcher_anim_y = 0;   /* current slide offset (0=fully open, neg=hidden) */

/* Launcher button */
#define LNCHR_BTN_X     4
#define LNCHR_BTN_Y     4
#define LNCHR_BTN_W    64
#define LNCHR_BTN_H    22

/* Glass panel — immediately below the button */
#define LNCHR_PANEL_X   4
#define LNCHR_PANEL_Y  (LNCHR_BTN_Y + LNCHR_BTN_H + 3)
#define LNCHR_PANEL_W  250

/* Item geometry */
#define LNCHR_ITEM_H   18    /* height per app entry */
#define LNCHR_HDR_H    20    /* height per category header */
#define LNCHR_ICON_W   12    /* small coloured icon square */

/* ELF browser panel */
#define LNCHR_BROWSER_W    200
#define LNCHR_BROWSER_MAX   20

/* ---- Launcher item table ----
 * action == -1  → non-clickable category header
 * action == 0   → wm_spawn TERMINAL
 * action == 1   → wm_spawn CALC
 * action == 2   → wm_spawn STEXT
 * action == 3+N → wm_spawn_kapp(N)   (N = KAPP_*)
 * action == 19  → ELF browser (was 18 in old code; kept at 18 below)
 */
typedef struct {
    const char *label;
    int         action;       /* -1=header, else as above */
    uint32_t    icon_color;   /* background of the icon square */
    char        icon_char;    /* letter drawn inside the icon */
} lnchr_item_t;

static const lnchr_item_t lnchr_items[] = {
    /* ---- SYSTEM ---- */
    { "SYSTEM",           -1,  0x000000, ' ' },
    { "STerm",             0,  0x003311, 'T' },
    { "Calculator",        1,  0x002A15, 'C' },
    { "SText",             2,  0x002010, 'E' },
    /* ---- UTILITIES ---- */
    { "UTILITIES",        -1,  0x000000, ' ' },
    { "Clock",             3,  0x001A22, 'K' },
    { "System Info",       5,  0x001A10, 'I' },
    { "Task Manager",      6,  0x001A10, 'M' },
    { "Paint",             7,  0x001A0A, 'P' },
    { "Notepad",           8,  0x001A0A, 'N' },
    { "File Manager",      9,  0x001A0A, 'F' },
    { "File Viewer",      10,  0x001A0A, 'V' },
    { "Settings",         11,  0x001520, 'S' },
    /* ---- GAMES ---- */
    { "GAMES",            -1,  0x000000, ' ' },
    { "Snake",            12,  0x0A2005, 'S' },
    { "Breakout",         13,  0x1A1005, 'B' },
    { "Pong",             14,  0x051A1A, 'G' },
    { "2048",             15,  0x0A2005, '4' },
    { "SiMPLE Racer",      4,  0x1A1005, 'R' },
    { "Speedway",         16,  0x1A0A05, 'W' },
    { "Constitution",     17,  0x08140A, 'D' },
    /* ---- APPS ---- */
    { "APPS",             -1,  0x000000, ' ' },
    { "Run App...",       18,  0x050F1A, 'A' },
};
#define LNCHR_NTOTAL  23

/* FAT16 filesystem reference — set by wm_set_fs() from shell_run */
static fat16_fs_t    *wm_fs = (fat16_fs_t *)0;
static fat16_dirent_t wm_elf_entries[LNCHR_BROWSER_MAX];
static int            wm_elf_count = 0;

/*
 * No re-entrancy guard needed here.
 *
 * When current_proc < 0 (no ring-3 process running) the launcher uses the
 * blocking exec_elf() + launch_ring3() path, which saves kernel_esp so that
 * exit_trampoline can return control when the last process exits.
 *
 * When current_proc >= 0 (a ring-3 process is already running and called
 * SYS_WM_EVENT → wm_pump_input → wm_handle_mouse), the launcher uses the
 * non-blocking exec_elf_spawn() path, which loads the new ELF into a
 * dedicated physical memory slot and marks it PROC_RUNNABLE.  The preemptive
 * PIT timer will schedule it on the next tick.  No kmalloc_reset(), no
 * nested launch_ring3() — safe to call from within a syscall handler.
 */

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
 * Green Glass colour palette — Military Cyberpunk aesthetic
 * ================================================================ */

/* Desktop */
#define COL_DESKTOP          0x030A05   /* near-black green-tinted desktop      */
#define COL_WALLPAPER_TINT   0x031005   /* green overlay tint on wallpaper      */

/* Window border/frame */
#define COL_BORDER_ACT       0x22EE55   /* vivid green border, focused          */
#define COL_BORDER_INACT     0x0F2A18   /* muted border, unfocused              */

/* Titlebar gradient — active */
#define COL_TITLE_TOP_ACT    0x1AEE66   /* active: bright mint top              */
#define COL_TITLE_MID_ACT    0x00AA44   /* active: emerald middle               */
#define COL_TITLE_BOT_ACT    0x004422   /* active: deep shadow bottom           */

/* Titlebar gradient — inactive */
#define COL_TITLE_TOP_INAC   0x0E4A22   /* inactive: dim forest top             */
#define COL_TITLE_MID_INAC   0x072A14   /* inactive: deep forest middle         */
#define COL_TITLE_BOT_INAC   0x030F08   /* inactive: near-black bottom          */

/* Titlebar text */
#define COL_TITLEFG_ACT      0xEEFFEE   /* active: near-white title text        */
#define COL_TITLEFG_INACT    0x558866   /* inactive: dim green title text       */

/* Client area — glass panel effect */
#define COL_GLASS_TINT       0x041408   /* very dark green glass tint           */
#define COL_GLASS_EDGE       0x0D3318   /* glass panel inner edge highlight     */

/* Calculator / display */
#define COL_DISPBG           0x010A03   /* dark display bg                      */
#define COL_DISPFG           0x00FF88   /* bright mint display fg               */
#define COL_BTNBG            0x061A0C   /* dark green button fill               */
#define COL_BTNBDR           0x22AA44   /* green button border                  */
#define COL_BTNFG            0xBBFFCC   /* light-green button text              */

/* Close button — crimson with hover style */
#define COL_CLOSE_FILL       0x991A28   /* close button fill                    */
#define COL_CLOSE_FILL_HOV   0xEE2244   /* close button hover                   */
#define COL_CLOSE_EDGE       0xFF5566   /* close button highlight edge          */
#define COL_CLOSE_FG         0xFFEEEE   /* close button X colour                */

/* Launcher panel */
#define COL_LNCHR_BTN_BG     0x061A0D   /* launcher button background           */
#define COL_LNCHR_BTN_BD     0x22EE55   /* launcher button border               */
#define COL_PANEL_BG         0x040E07   /* launcher glass panel bg (very dark)  */
#define COL_PANEL_BD         0x22AA44   /* launcher glass panel border          */
#define COL_PANEL_FG         0xAAFFBB   /* launcher normal item text            */
#define COL_PANEL_HDR_BG     0x061A0C   /* category header background           */
#define COL_PANEL_HDR_FG     0x44FF88   /* category header text                 */
#define COL_PANEL_HOVER_BG   0x0A2A14   /* item hover background                */
#define COL_PANEL_HOVER_FG   0xCCFFDD   /* item hover text                      */
#define COL_PANEL_OPEN_FG    0x33EE66   /* already-open kapp indicator          */
#define COL_PANEL_DIM_FG     0x446655   /* dimmed (unavailable) item text       */
#define COL_PANEL_ACCENT     0x1AEE55   /* left accent bar on category headers  */
#define COL_PANEL_SEP        0x0A2A14   /* separator line colour                */

/* SText editor */
#define COL_STEXT_FG         0x88FFAA   /* editor text colour                   */

/* Glow layers (active window ambient glow) */
#define COL_GLOW_1           0x00330F   /* innermost glow layer                 */
#define COL_GLOW_2           0x00220A   /* middle glow layer                    */
#define COL_GLOW_3           0x001107   /* outer glow layer                     */

/* Shadow layers */
#define COL_SHADOW_NEAR      0x00000088 /* drop shadow near (unused: not RGBA)  */

/* ---- Permanent UI chrome heights ---- */
#define UI_MENUBAR_H  24   /* top menu bar height (px)  */
#define UI_DOCK_H     52   /* bottom dock height  (px)  */

/* ---- Dock button geometry ---- */
#define DOCK_BTN_W    64
#define DOCK_BTN_H    40
#define DOCK_BTN_GAP  12
#define DOCK_NITEMS    3

/* ================================================================
 * Rounded-corner tables (radius = WM_CORNER_R = 8)
 *
 * corner_insets[r] = number of pixels to erase from each corner edge
 * at row r from the corner.  Computed from: inset = R - sqrt(R²-(R-r)²)
 * ================================================================ */
static const int corner_insets[WM_CORNER_R] = { 8, 4, 3, 2, 1, 0, 0, 0 };

/* Per-corner saved background pixels (read before window is drawn) */
static uint32_t csave_tl[WM_CORNER_R][WM_CORNER_R];
static uint32_t csave_tr[WM_CORNER_R][WM_CORNER_R];
static uint32_t csave_bl[WM_CORNER_R][WM_CORNER_R];
static uint32_t csave_br[WM_CORNER_R][WM_CORNER_R];

/* Save corner pixels from fb_back BEFORE drawing this window */
static void save_corners(int wx, int wy, int ww, int wh) {
    int R = WM_CORNER_R;
    for (int r = 0; r < R; r++) {
        for (int c = 0; c < R; c++) {
            csave_tl[r][c] = fb_read_pixel(wx + c,          wy + r);
            csave_tr[r][c] = fb_read_pixel(wx + ww - R + c, wy + r);
            csave_bl[r][c] = fb_read_pixel(wx + c,          wy + wh - R + r);
            csave_br[r][c] = fb_read_pixel(wx + ww - R + c, wy + wh - R + r);
        }
    }
}

/* Restore saved corner pixels to create rounded-corner illusion */
static void apply_corners(int wx, int wy, int ww, int wh) {
    int R = WM_CORNER_R;
    for (int r = 0; r < R; r++) {
        int inset = corner_insets[r];
        for (int c = 0; c < inset && c < R; c++) {
            /* top-left */
            fb_fill_rect(wx + c,          wy + r,         1, 1, csave_tl[r][c]);
            /* top-right (mirror horizontally) */
            fb_fill_rect(wx + ww - 1 - c, wy + r,         1, 1, csave_tr[r][R - 1 - c]);
            /* bottom-left (mirror vertically) */
            fb_fill_rect(wx + c,          wy + wh - 1 - r, 1, 1, csave_bl[R - 1 - r][c]);
            /* bottom-right (mirror both) */
            fb_fill_rect(wx + ww - 1 - c, wy + wh - 1 - r, 1, 1, csave_br[R - 1 - r][R - 1 - c]);
        }
    }
}

/* Draw a single-pixel rounded border on top of an existing window rect */
static void draw_rounded_border(int wx, int wy, int ww, int wh, uint32_t col) {
    int R = WM_CORNER_R;
    /* top / bottom horizontal runs (skip corner columns) */
    fb_fill_rect(wx + R, wy,         ww - 2 * R, 1, col);
    fb_fill_rect(wx + R, wy + wh - 1, ww - 2 * R, 1, col);
    /* left / right vertical runs (skip corner rows) */
    fb_fill_rect(wx,         wy + R, 1, wh - 2 * R, col);
    fb_fill_rect(wx + ww - 1, wy + R, 1, wh - 2 * R, col);
    /* Approximate arc pixels for each corner */
    for (int r = 0; r < R; r++) {
        int inset = corner_insets[r];
        int next_inset = (r + 1 < R) ? corner_insets[r + 1] : 0;
        /* The border pixel at each corner arc row is the first non-erased column */
        if (inset > 0 && inset <= ww / 2) {
            /* top-left arc pixel */
            fb_fill_rect(wx + inset - 1, wy + r, 1, 1, col);
            /* top-right arc pixel */
            fb_fill_rect(wx + ww - inset, wy + r, 1, 1, col);
            /* bottom-left arc pixel */
            fb_fill_rect(wx + inset - 1, wy + wh - 1 - r, 1, 1, col);
            /* bottom-right arc pixel */
            fb_fill_rect(wx + ww - inset, wy + wh - 1 - r, 1, 1, col);
        }
        (void)next_inset;
    }
}

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
    int wx = w->x, wy = w->y, ww = w->width, wh = w->height;

    /* Save the four corners of fb_back BEFORE we paint anything */
    save_corners(wx, wy, ww, wh);

    /* ---- Ambient glow: concentric green rings around active window ---- */
    if (is_active) {
        fb_fill_rect_alpha(wx - 6, wy - 6, ww + 12, wh + 12, COL_GLOW_3, 90);
        fb_fill_rect_alpha(wx - 4, wy - 4, ww +  8, wh +  8, COL_GLOW_2, 110);
        fb_fill_rect_alpha(wx - 2, wy - 2, ww +  4, wh +  4, COL_GLOW_1, 130);
        /* Overwrite the glow's interior so only the ring is visible */
        fb_fill_rect_alpha(wx,     wy,     ww,       wh,      COL_DESKTOP, 80);
    }

    /* ---- Multi-layer soft drop shadow ---- */
    fb_fill_rect_alpha(wx + 8, wy + 8, ww + 2, wh + 2, 0x000000, 90);
    fb_fill_rect_alpha(wx + 5, wy + 5, ww + 2, wh + 2, 0x000000, 70);
    fb_fill_rect_alpha(wx + 3, wy + 3, ww + 1, wh + 1, 0x000000, 50);

    /* ---- Window base fill ---- */
    uint32_t border_col = is_active ? COL_BORDER_ACT : COL_BORDER_INACT;
    fb_fill_rect(wx, wy, ww, wh, border_col);

    /* ---- Gradient titlebar ---- */
    int tx  = wx + WM_BORDER;
    int ty  = wy + WM_BORDER;
    int tw  = ww - 2 * WM_BORDER;
    int tth = WM_TITLEBAR_H - WM_BORDER;   /* inner titlebar height */

    if (is_active) {
        fb_fill_gradient_v(tx, ty, tw, tth,
                           COL_TITLE_TOP_ACT, COL_TITLE_BOT_ACT);
        /* Gloss highlight: top 3 rows slightly brighter */
        fb_fill_rect_alpha(tx, ty, tw, 3, COL_TITLE_TOP_ACT, 100);
    } else {
        fb_fill_gradient_v(tx, ty, tw, tth,
                           COL_TITLE_TOP_INAC, COL_TITLE_BOT_INAC);
    }

    /* Separator line between titlebar and client area */
    fb_fill_rect(tx, wy + WM_TITLEBAR_H - 1, tw, 1,
                 is_active ? 0x11CC44 : 0x0A2218);

    /* Title text — fg-only so gradient shows through */
    uint32_t title_fg = is_active ? COL_TITLEFG_ACT : COL_TITLEFG_INACT;
    fb_draw_string_px_fg(tx + 10, ty + (tth - 8) / 2, w->title, title_fg);

    /* ---- Close button — 14×14 px, vertically centred in titlebar ---- */
    int cbx = wx + ww - 4 - 14;
    int cby = wy + (WM_TITLEBAR_H - 14) / 2;
    /* Rounded-ish fill */
    fb_fill_rect(cbx,     cby,     14, 14, COL_CLOSE_FILL);
    /* Gloss top edge */
    fb_fill_rect(cbx,     cby,     14,  2, 0xDD3355);
    /* Highlight edges */
    fb_fill_rect(cbx,     cby,     14,  1, COL_CLOSE_EDGE);
    fb_fill_rect(cbx,     cby,      1, 14, COL_CLOSE_EDGE);
    /* Shadow edges */
    fb_fill_rect(cbx,     cby + 13, 14, 1, 0x550011);
    fb_fill_rect(cbx + 13, cby,      1, 14, 0x550011);
    /* × glyph */
    fb_draw_string_px_fg(cbx + 3, cby + 3, "X", COL_CLOSE_FG);

    /* ---- Glass client area ---- */
    int cay = wy + WM_TITLEBAR_H;
    int cah = wh - WM_TITLEBAR_H - WM_BORDER;
    /* Semi-transparent dark green glass over existing framebuffer content */
    fb_fill_rect_alpha(tx, cay, tw, cah, COL_GLASS_TINT, 210);
    /* Subtle inner edge highlight along the top of the client area */
    fb_fill_rect_alpha(tx, cay, tw, 1, COL_GLASS_EDGE, 180);
    /* Very subtle left/right inner edge */
    fb_fill_rect_alpha(tx,          cay, 1, cah, COL_GLASS_EDGE, 80);
    fb_fill_rect_alpha(tx + tw - 1, cay, 1, cah, COL_GLASS_EDGE, 80);

    /* ---- Resize corner accent marks ---- */
    uint32_t rc = is_active ? 0x33FF77 : 0x0F3322;
    fb_fill_rect(wx + ww - RESIZE_HANDLE, wy + wh - WM_BORDER,
                 RESIZE_HANDLE, WM_BORDER, rc);
    fb_fill_rect(wx + ww - WM_BORDER, wy + wh - RESIZE_HANDLE,
                 WM_BORDER, RESIZE_HANDLE, rc);
    fb_fill_rect(wx,          wy + wh - WM_BORDER,
                 RESIZE_HANDLE, WM_BORDER, rc);
    fb_fill_rect(wx,          wy + wh - RESIZE_HANDLE,
                 WM_BORDER, RESIZE_HANDLE, rc);

    /* ---- Rounded corners: restore saved background pixels ---- */
    apply_corners(wx, wy, ww, wh);

    /* ---- Rounded border painted on top of corner arcs ---- */
    draw_rounded_border(wx, wy, ww, wh, border_col);

    /* ---- Open-fade animation overlay ---- */
    if (w->anim_alpha < 255) {
        uint8_t overlay = (uint8_t)(255u - w->anim_alpha);
        fb_fill_rect_alpha(wx, wy, ww, wh, COL_DESKTOP, overlay);
        /* Advance towards fully-opaque */
        uint8_t step = 30;
        w->anim_alpha = (w->anim_alpha + step > 255u) ? 255u
                        : (uint8_t)(w->anim_alpha + step);
    }
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

/* Draw one button: filled rect + beveled border + centred label. */
static void draw_calc_button(int bx, int by, const char *label) {
    /* fill */
    fb_fill_rect(bx, by, CALC_BTN_W, CALC_BTN_H, COL_BTNBG);
    /* gloss highlight (top 2 rows) */
    fb_fill_rect(bx, by, CALC_BTN_W, 2, 0x1A4A28);
    /* top/left highlight edges */
    fb_fill_rect(bx,                  by,                  CALC_BTN_W, 1, COL_BTNBDR);
    fb_fill_rect(bx,                  by,                  1,          CALC_BTN_H, COL_BTNBDR);
    /* bottom/right shadow edges */
    fb_fill_rect(bx,                  by + CALC_BTN_H - 1, CALC_BTN_W, 1, 0x051008);
    fb_fill_rect(bx + CALC_BTN_W - 1, by,                  1,          CALC_BTN_H, 0x051008);
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
    /* Display border */
    fb_fill_rect(cx + 4, cy + 4, cw - 8, 1, 0x116622u);
    fb_fill_rect(cx + 4, cy + 4, 1, 20, 0x116622u);
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
            fb_draw_string_px(cx, ty, si->buf[line_idx], COL_STEXT_FG, 0xFF000000u);
        /* Lines below si->nlines are already dark from chrome fill — skip */
    }

    /* Cursor: invert the character cell under the cursor */
    int vis_row = si->cy - si->scroll;
    if (vis_row >= 0 && vis_row < STEXT_VIS_ROWS) {
        char cur_ch[2];
        cur_ch[0] = si->buf[si->cy][si->cx];
        if (!cur_ch[0]) cur_ch[0] = ' ';
        cur_ch[1] = '\0';
        fb_draw_string_px(cx + si->cx * 8, cy + vis_row * 8,
                          cur_ch, COL_GLASS_TINT, COL_STEXT_FG);
    }
}

/* ================================================================
 * Launcher bar rendering
 * ================================================================ */

/* Forward declaration for point_in_rect (defined later in this file) */
static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh);

/* ---- Compute total launcher panel height from the item table ---- */
static int lnchr_panel_height(void) {
    int h = 4;   /* top/bottom padding */
    for (int i = 0; i < LNCHR_NTOTAL; i++) {
        h += (lnchr_items[i].action < 0) ? LNCHR_HDR_H : LNCHR_ITEM_H;
    }
    return h;
}

/* Draw the glass "APPS" button */
static void draw_launcher_button(int hovered) {
    int bx = LNCHR_BTN_X, by = LNCHR_BTN_Y;
    int bw = LNCHR_BTN_W, bh = LNCHR_BTN_H;

    /* Glass base: semi-transparent dark green over whatever is behind */
    fb_fill_rect_alpha(bx, by, bw, bh, COL_LNCHR_BTN_BG, 220);
    /* Top gloss stripe */
    fb_fill_rect_alpha(bx, by, bw, 3, 0x33FF77, hovered ? 120 : 60);
    /* Bottom shadow stripe */
    fb_fill_rect_alpha(bx, by + bh - 2, bw, 2, 0x001008, 180);
    /* Border */
    fb_fill_rect(bx,          by,          bw, 1, COL_LNCHR_BTN_BD);
    fb_fill_rect(bx,          by + bh - 1, bw, 1, COL_LNCHR_BTN_BD);
    fb_fill_rect(bx,          by,          1, bh, COL_LNCHR_BTN_BD);
    fb_fill_rect(bx + bw - 1, by,          1, bh, COL_LNCHR_BTN_BD);
    /* Active indicator when panel is open */
    if (launcher_open)
        fb_fill_rect(bx, by + bh - 1, bw, 2, COL_LNCHR_BTN_BD);
    /* Label — fg-only so glass background shows through */
    uint32_t lbl_col = hovered ? 0xCCFFDDu : 0x88FFAAU;
    fb_draw_string_px_fg(bx + (bw - 4*8) / 2, by + (bh - 8) / 2, "APPS", lbl_col);
}

/* Draw one launcher item row; returns 1 if it should be highlighted */
static void draw_lnchr_item(int ix, int iy, int iw, const lnchr_item_t *it,
                            int hovered, int open_kapp) {
    if (it->action < 0) {
        /* Category header */
        fb_fill_rect(ix, iy, iw, LNCHR_HDR_H, COL_PANEL_HDR_BG);
        /* Left accent bar */
        fb_fill_rect(ix, iy, 3, LNCHR_HDR_H, COL_PANEL_ACCENT);
        /* Separator line below */
        fb_fill_rect(ix, iy + LNCHR_HDR_H - 1, iw, 1, COL_PANEL_SEP);
        fb_draw_string_px_fg(ix + 8, iy + (LNCHR_HDR_H - 8) / 2,
                             it->label, COL_PANEL_HDR_FG);
        return;
    }

    /* Regular item */
    uint32_t row_bg = hovered ? COL_PANEL_HOVER_BG : 0xFF000000u;
    if (hovered) fb_fill_rect(ix, iy, iw, LNCHR_ITEM_H, row_bg);

    /* Small coloured icon square */
    int icon_x = ix + 6;
    int icon_y = iy + (LNCHR_ITEM_H - LNCHR_ICON_W) / 2;
    fb_fill_rect(icon_x, icon_y, LNCHR_ICON_W, LNCHR_ICON_W, it->icon_color);
    /* Icon border */
    fb_fill_rect(icon_x,                  icon_y,                  LNCHR_ICON_W, 1, 0x22AA44u);
    fb_fill_rect(icon_x,                  icon_y + LNCHR_ICON_W-1, LNCHR_ICON_W, 1, 0x22AA44u);
    fb_fill_rect(icon_x,                  icon_y,                  1, LNCHR_ICON_W, 0x22AA44u);
    fb_fill_rect(icon_x + LNCHR_ICON_W-1, icon_y,                  1, LNCHR_ICON_W, 0x22AA44u);
    /* Icon letter — fg-only over the icon square */
    char ibuf[2] = { it->icon_char, '\0' };
    fb_draw_string_px_fg(icon_x + 2, icon_y + 2, ibuf, 0x44FF88u);

    /* Item label */
    uint32_t fg;
    if      (it->action == 18 && !wm_fs)     fg = COL_PANEL_DIM_FG;
    else if (open_kapp)                       fg = COL_PANEL_OPEN_FG;
    else if (hovered)                         fg = COL_PANEL_HOVER_FG;
    else                                      fg = COL_PANEL_FG;

    fb_draw_string_px_fg(icon_x + LNCHR_ICON_W + 6,
                         iy + (LNCHR_ITEM_H - 8) / 2,
                         it->label, fg);

    /* Subtle bottom separator */
    fb_fill_rect_alpha(ix + 4, iy + LNCHR_ITEM_H - 1, iw - 8, 1,
                       COL_PANEL_SEP, 120);
}

static void draw_launcher(void) {
    int mx = mouse_get_x();
    int my = mouse_get_y();

    /* Hover detection for the button itself */
    int btn_hov = point_in_rect(mx, my, LNCHR_BTN_X, LNCHR_BTN_Y,
                                LNCHR_BTN_W, LNCHR_BTN_H);
    draw_launcher_button(btn_hov || launcher_open);

    if (!launcher_open) return;

    /* ---- Slide-in animation: advance launcher_anim_y toward 0 ---- */
    if (launcher_open == 1 || launcher_open == 2) {
        if (launcher_anim_y < 0) {
            launcher_anim_y += 30;
            if (launcher_anim_y > 0) launcher_anim_y = 0;
        }
    }

    if (launcher_open == 1) {
        /* ---- Main glass panel ---- */
        int panel_h = lnchr_panel_height();
        int px  = LNCHR_PANEL_X;
        int py  = LNCHR_PANEL_Y + launcher_anim_y;
        int pw  = LNCHR_PANEL_W;

        /* Glass base: very dark semi-transparent green */
        fb_fill_rect_alpha(px, py, pw, panel_h, COL_PANEL_BG, 230);
        /* Top gloss stripe */
        fb_fill_rect_alpha(px, py, pw, 2, 0x33FF77, 60);
        /* Border */
        fb_fill_rect(px,          py,              pw, 1,       COL_PANEL_BD);
        fb_fill_rect(px,          py + panel_h - 1, pw, 1,       COL_PANEL_BD);
        fb_fill_rect(px,          py,              1, panel_h, COL_PANEL_BD);
        fb_fill_rect(px + pw - 1, py,              1, panel_h, COL_PANEL_BD);
        /* Inner border (second border for depth) */
        fb_fill_rect_alpha(px + 1, py + 1, pw - 2, 1, COL_PANEL_BD, 80);
        fb_fill_rect_alpha(px + 1, py + 1, 1, panel_h - 2, COL_PANEL_BD, 80);

        /* Draw items */
        int iy = py + 2;
        for (int i = 0; i < LNCHR_NTOTAL; i++) {
            const lnchr_item_t *it = &lnchr_items[i];
            int row_h = (it->action < 0) ? LNCHR_HDR_H : LNCHR_ITEM_H;
            int item_hov = (it->action >= 0) &&
                           point_in_rect(mx, my, px + 1, iy, pw - 2, row_h);
            int is_open  = (it->action >= 3 && it->action <= 17) &&
                           kapp_is_open(it->action - 3);
            draw_lnchr_item(px + 1, iy, pw - 2, it, item_hov, is_open);
            iy += row_h;
        }

    } else if (launcher_open == 2) {
        /* ---- ELF browser panel ---- */
        int rows     = wm_elf_count + 1;
        if (rows < 2) rows = 2;
        int browser_h = rows * LNCHR_ITEM_H + 6;
        int px = LNCHR_PANEL_X;
        int py = LNCHR_PANEL_Y + launcher_anim_y;

        fb_fill_rect_alpha(px, py, LNCHR_BROWSER_W, browser_h, COL_PANEL_BG, 230);
        fb_fill_rect_alpha(px, py, LNCHR_BROWSER_W, 2, 0x33FF77, 60);
        fb_fill_rect(px,                         py,               LNCHR_BROWSER_W, 1,          COL_PANEL_BD);
        fb_fill_rect(px,                         py + browser_h-1, LNCHR_BROWSER_W, 1,          COL_PANEL_BD);
        fb_fill_rect(px,                         py,               1,               browser_h,  COL_PANEL_BD);
        fb_fill_rect(px + LNCHR_BROWSER_W - 1,   py,               1,               browser_h,  COL_PANEL_BD);

        /* "< BACK" header row */
        int back_hov = point_in_rect(mx, my, px + 1, py + 3, LNCHR_BROWSER_W - 2, LNCHR_ITEM_H);
        if (back_hov) fb_fill_rect(px + 1, py + 3, LNCHR_BROWSER_W - 2, LNCHR_ITEM_H, COL_PANEL_HOVER_BG);
        fb_draw_string_px_fg(px + 8, py + 3 + (LNCHR_ITEM_H - 8) / 2,
                             "< BACK", back_hov ? 0xFFCCCCu : 0xFF9999u);
        fb_fill_rect_alpha(px + 4, py + 3 + LNCHR_ITEM_H - 1,
                           LNCHR_BROWSER_W - 8, 1, COL_PANEL_SEP, 120);

        /* ELF file entries */
        if (wm_elf_count == 0) {
            fb_draw_string_px_fg(px + 8,
                                 py + 3 + LNCHR_ITEM_H + (LNCHR_ITEM_H - 8) / 2,
                                 "No .elf files found", COL_PANEL_DIM_FG);
        } else {
            for (int i = 0; i < wm_elf_count; i++) {
                int ey = py + 3 + (i + 1) * LNCHR_ITEM_H;
                int ehov = point_in_rect(mx, my, px + 1, ey, LNCHR_BROWSER_W - 2, LNCHR_ITEM_H);
                if (ehov) fb_fill_rect(px + 1, ey, LNCHR_BROWSER_W - 2, LNCHR_ITEM_H, COL_PANEL_HOVER_BG);
                fb_draw_string_px_fg(px + 8, ey + (LNCHR_ITEM_H - 8) / 2,
                                     wm_elf_entries[i].name,
                                     ehov ? COL_PANEL_HOVER_FG : COL_PANEL_FG);
                fb_fill_rect_alpha(px + 4, ey + LNCHR_ITEM_H - 1,
                                   LNCHR_BROWSER_W - 8, 1, COL_PANEL_SEP, 120);
            }
        }
    }
}

/* ================================================================
 * Mouse cursor
 * ================================================================ */

#define CUR_ARM 5

static void draw_cursor(int x, int y) {
    int len = CUR_ARM * 2 + 1;
    /* Dark outline for contrast */
    fb_fill_rect(x - CUR_ARM - 1, y - 1,           len + 2, 3,       0x001A08);
    fb_fill_rect(x - 1,           y - CUR_ARM - 1, 3,       len + 2, 0x001A08);
    /* Bright green cross */
    fb_fill_rect(x - CUR_ARM, y,           len, 1, 0x44FF88);
    fb_fill_rect(x,           y - CUR_ARM, 1,   len, 0x44FF88);
    /* Bright dot at centre */
    fb_fill_rect(x - 1, y - 1, 3, 3, 0xAAFFCC);
    fb_fill_rect(x, y, 1, 1, 0xFFFFFF);
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

/* 1 if (px,py) is inside the close button (14×14 at top-right of title bar) */
static int point_in_close_btn(const wm_window_t *w, int px, int py) {
    int cbx = w->x + w->width - 4 - 14;
    int cby = w->y + (WM_TITLEBAR_H - 14) / 2;
    return point_in_rect(px, py, cbx, cby, 14, 14);
}

/* Bottom-right corner resize handle */
static int point_in_resize_br(const wm_window_t *w, int px, int py) {
    return point_in_rect(px, py,
                         w->x + w->width  - RESIZE_HANDLE,
                         w->y + w->height - RESIZE_HANDLE,
                         RESIZE_HANDLE, RESIZE_HANDLE);
}

/* Bottom-left corner resize handle */
static int point_in_resize_bl(const wm_window_t *w, int px, int py) {
    return point_in_rect(px, py,
                         w->x,
                         w->y + w->height - RESIZE_HANDLE,
                         RESIZE_HANDLE, RESIZE_HANDLE);
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
 * Also implements single-active-ELF mode: when focus switches between
 * USER-window-owned process slots, the outgoing slot is frozen (PROC_STOPPED)
 * and the incoming slot is thawed (PROC_RUNNABLE).  This ensures only the
 * focused GUI ELF consumes CPU; background ELFs are completely frozen.
 *
 * Safe to call even when old wm_active is hidden or the same as new_idx.
 */
static void wm_set_active(int new_idx) {
    int old_idx = wm_active;

    /* Save outgoing terminal session into its struct */
    if (!wm_windows[old_idx].hidden &&
        wm_windows[old_idx].type == WM_TYPE_TERMINAL)
        vga_save_session(&term_sessions[wm_windows[old_idx].instance]);

    /* ---- Single-active-ELF mode ----------------------------------------
     * Determine the process slot that owns each window.  Only USER windows
     * (ring-3 ELF pixel-buffer windows) have a meaningful owner_slot.
     * Non-USER windows (terminal, calc, kapp) use -1 so we leave ring-3
     * processes unaffected when focus moves to/from kernel-owned windows.
     * -------------------------------------------------------------------- */
    if (old_idx != new_idx) {
        int old_slot = (!wm_windows[old_idx].hidden &&
                        wm_windows[old_idx].type == WM_TYPE_USER)
                       ? wm_windows[old_idx].owner_slot : -1;
        int new_slot = (!wm_windows[new_idx].hidden &&
                        wm_windows[new_idx].type == WM_TYPE_USER)
                       ? wm_windows[new_idx].owner_slot : -1;

        /* Suspend the outgoing ELF if it is different from the incoming one. */
        if (old_slot >= 0 && old_slot < MAX_PROCS && old_slot != new_slot) {
            proc_state_t s = proc_table[old_slot].state;
            if (s == PROC_RUNNING || s == PROC_RUNNABLE) {
                proc_table[old_slot].state = PROC_STOPPED;
                serial_write(COM1, "[WM] focus-suspend slot=");
                serial_write_dec(COM1, (uint32_t)old_slot);
                serial_write(COM1, " (old_wid=");
                serial_write_dec(COM1, (uint32_t)old_idx);
                serial_write(COM1, " → new_wid=");
                serial_write_dec(COM1, (uint32_t)new_idx);
                serial_write(COM1, ")\n");
            }
        }

        /* Resume the incoming ELF if it was frozen. */
        if (new_slot >= 0 && new_slot < MAX_PROCS) {
            if (proc_table[new_slot].state == PROC_STOPPED) {
                proc_table[new_slot].state = PROC_RUNNABLE;
                serial_write(COM1, "[WM] focus-resume slot=");
                serial_write_dec(COM1, (uint32_t)new_slot);
                serial_write(COM1, " (new_wid=");
                serial_write_dec(COM1, (uint32_t)new_idx);
                serial_write(COM1, ")\n");
            }
        }
    }

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

    w             = &wm_windows[wi];
    w->type       = type;
    w->instance   = inst;
    w->hidden     = 0;
    w->anim_alpha = 0;   /* start fully transparent — fades in on first draw */

    /* Available vertical range: below menu bar, above dock */
    int avail_y  = UI_MENUBAR_H;
    int avail_h  = scr_h - UI_DOCK_H - UI_MENUBAR_H;

    if (type == WM_TYPE_TERMINAL) {
        w->x = 2 + offset;  w->y = avail_y + 2 + offset;
        w->width = WM_TERM_W;  w->height = WM_TERM_H;
        w->title = "STerm";
    } else if (type == WM_TYPE_CALC) {
        w->x = (scr_w - WM_CALC_W)  / 2 + offset;
        w->y = avail_y + (avail_h - WM_CALC_H)  / 2 + offset;
        w->width = WM_CALC_W;  w->height = WM_CALC_H;
        w->title = "Calculator";
    } else {
        w->x = (scr_w - WM_STEXT_W) / 2 + offset;
        w->y = avail_y + (avail_h - WM_STEXT_H) / 2 + offset;
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
    else if (w->type == WM_TYPE_STEXT)
        free_inst(stext_used, w->instance);
    else if (w->type == WM_TYPE_KAPP) {
        kapp_close(w->instance, idx);
    } else {
        /* WM_TYPE_USER: notify the app via CLOSE event, reclaim pixel store */
        wm_event_t ev;
        ev.type = 5u; ev.wid = (uint16_t)idx;
        ev.x = 0; ev.y = 0; ev.btn = 0;
        wm_push_to_slot(w->owner_slot, ev);

        /* If the owner was frozen by the focus system, wake it so it can
         * dequeue the CLOSE event and exit.  Without this the process stays
         * PROC_STOPPED forever and the slot is never reclaimed. */
        int cs = w->owner_slot;
        if (cs >= 0 && cs < MAX_PROCS &&
            proc_table[cs].state == PROC_STOPPED) {
            proc_table[cs].state = PROC_RUNNABLE;
            serial_write(COM1, "[WM] wake-for-close slot=");
            serial_write_dec(COM1, (uint32_t)cs);
            serial_write(COM1, "\n");
        }

        pixbuf_free(w->pixels);
        w->pixels = (uint32_t *)0;
    }

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
static void handle_client_click(wm_window_t *w, int wi, int x, int y) {
    int cx = w->x + WM_BORDER;
    int cy = w->y + WM_TITLEBAR_H;
    int rx = x - cx;
    int ry = y - cy;

    if (w->type == WM_TYPE_CALC) {
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
    } else if (w->type == WM_TYPE_KAPP) {
        kapp_handle_click(w->instance, wi, rx, ry);
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
            if (launcher_open) {
                launcher_open   = 0;
            } else {
                launcher_open   = 1;
                launcher_anim_y = -lnchr_panel_height();  /* slide in from above */
            }
            launcher_handled = 1;
        }

        /* ---- 2a. Launcher main glass panel (launcher_open == 1) ---- */
        if (!launcher_handled && launcher_open == 1) {
            int panel_h = lnchr_panel_height();
            int py = LNCHR_PANEL_Y + launcher_anim_y;
            if (point_in_rect(x, y, LNCHR_PANEL_X, py, LNCHR_PANEL_W, panel_h)) {
                /* Map click Y to item in lnchr_items[] */
                int iy = py + 2;
                int action = -2;  /* -2 = no hit */
                for (int i = 0; i < LNCHR_NTOTAL; i++) {
                    const lnchr_item_t *it = &lnchr_items[i];
                    int row_h = (it->action < 0) ? LNCHR_HDR_H : LNCHR_ITEM_H;
                    if (it->action >= 0 && y >= iy && y < iy + row_h) {
                        action = it->action;
                        break;
                    }
                    iy += row_h;
                }
                if (action == 0) {
                    wm_spawn(WM_TYPE_TERMINAL);   launcher_open = 0;
                } else if (action == 1) {
                    wm_spawn(WM_TYPE_CALC);       launcher_open = 0;
                } else if (action == 2) {
                    wm_spawn(WM_TYPE_STEXT);      launcher_open = 0;
                } else if (action >= 3 && action <= 17) {
                    wm_spawn_kapp(action - 3);    launcher_open = 0;
                } else if (action == 18 && wm_fs) {
                    fat16_dirent_t tmp[32];
                    int total = 0;
                    fat16_list_entries(wm_fs, 0, tmp, 32, &total);
                    wm_elf_count = 0;
                    for (int ei = 0; ei < total && wm_elf_count < LNCHR_BROWSER_MAX; ei++) {
                        if (tmp[ei].attr & 0x10u) continue;
                        char *n = tmp[ei].name;
                        int l = (int)strlen(n);
                        if (l >= 4 && n[l-4] == '.' &&
                            (n[l-3]=='E'||n[l-3]=='e') &&
                            (n[l-2]=='L'||n[l-2]=='l') &&
                            (n[l-1]=='F'||n[l-1]=='f')) {
                            wm_elf_entries[wm_elf_count++] = tmp[ei];
                        }
                    }
                    launcher_open   = 2;
                    launcher_anim_y = -((wm_elf_count + 2) * LNCHR_ITEM_H + 6);
                } else if (action == -2) {
                    /* click outside items but inside panel → ignore */
                }
                launcher_handled = 1;
            } else {
                launcher_open = 0;
            }
        }

        /* ---- 2b. ELF browser panel (launcher_open == 2) ---- */
        if (!launcher_handled && launcher_open == 2) {
            int rows = wm_elf_count + 1;
            if (rows < 2) rows = 2;
            int browser_h = rows * LNCHR_ITEM_H + 6;
            int py = LNCHR_PANEL_Y + launcher_anim_y;
            if (point_in_rect(x, y, LNCHR_PANEL_X, py,
                              LNCHR_BROWSER_W, browser_h)) {
                int item = (y - py - 3) / LNCHR_ITEM_H;
                if (item == 0) {
                    launcher_open   = 1;
                    launcher_anim_y = -lnchr_panel_height();
                } else {
                    int elf_idx = item - 1;
                    if (elf_idx >= 0 && elf_idx < wm_elf_count && wm_fs) {
                        launcher_open = 0;
                        wm_draw_all();

                        /*
                         * Always use the non-blocking exec_elf_spawn() path.
                         * The ring-0 shell stays live (STerm keeps updating),
                         * and proc_timer_tick switches to the new ring-3
                         * process on the next PIT tick.  When the last process
                         * exits, proc_exit restores the saved ring-0 ISR frame
                         * so the shell resumes transparently.
                         */
                        {
                            serial_write(COM1, "[launch] ELF browser click: file=");
                            serial_write(COM1, wm_elf_entries[elf_idx].name);
                            serial_write(COM1, " current_proc=");
                            if (current_proc < 0)
                                serial_write(COM1, "ring0");
                            else
                                serial_write_dec(COM1, (uint32_t)current_proc);
                            serial_write(COM1, " heap_used=");
                            serial_write_hex(COM1, kmalloc_used());
                            serial_write(COM1, "/");
                            serial_write_hex(COM1, kmalloc_total());
                            serial_write(COM1, "\n");
                            proc_dump_table("pre-launch");

                            int spawn_slot = proc_find_spawn_slot();
                            serial_write(COM1, "[launch] spawn_slot=");
                            if (spawn_slot < 0)
                                serial_write(COM1, "NONE (no free slots!)");
                            else
                                serial_write_dec(COM1, (uint32_t)spawn_slot);
                            serial_write(COM1, "\n");

                            if (spawn_slot >= 0) {
                                uint32_t phys = PROC_SLOT_PHYS(spawn_slot);
                                uint32_t elen = 0;
                                serial_write(COM1, "[launch] reading ELF to phys=");
                                serial_write_hex(COM1, phys);
                                serial_write(COM1, "\n");
                                int rc = fat16_read_file(wm_fs, 0,
                                             wm_elf_entries[elf_idx].name,
                                             (char *)phys, ELF_LOAD_BUF,
                                             &elen);
                                serial_write(COM1, "[launch] fat16_read rc=");
                                serial_write_dec(COM1, (uint32_t)(rc < 0 ? (uint32_t)(-rc) : (uint32_t)rc));
                                serial_write(COM1, " elen=");
                                serial_write_hex(COM1, elen);
                                serial_write(COM1, "\n");
                                if (rc == FAT16_OK && elen > 0) {
                                    int sr = exec_elf_spawn((void *)phys, elen,
                                                   spawn_slot);
                                    serial_write(COM1, "[launch] exec_elf_spawn rc=");
                                    serial_write_dec(COM1, (uint32_t)(sr < 0 ? (uint32_t)(-sr) : (uint32_t)sr));
                                    if (sr < 0) serial_write(COM1, " (FAILED)");
                                    serial_write(COM1, "\n");
                                } else {
                                    serial_write(COM1, "[launch] SKIP exec: fat16 failed or elen=0\n");
                                }
                            }
                        }
                    } else {
                        launcher_open = 0;
                    }
                }
                launcher_handled = 1;
            } else {
                /* Click outside browser → close */
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

                if (point_in_resize_br(&wm_windows[i], x, y)) {
                    /* Bottom-right resize: anchor = top-left corner */
                    drag_active  = 1;
                    drag_mode    = 2;
                    drag_win_idx = i;
                    drag_off_x   = (wm_windows[i].x + wm_windows[i].width)  - x;
                    drag_off_y   = (wm_windows[i].y + wm_windows[i].height) - y;
                } else if (point_in_resize_bl(&wm_windows[i], x, y)) {
                    /* Bottom-left resize: anchor = top-right corner */
                    drag_active       = 1;
                    drag_mode         = 3;
                    drag_win_idx      = i;
                    drag_off_x        = x - wm_windows[i].x;
                    drag_off_y        = (wm_windows[i].y + wm_windows[i].height) - y;
                    drag_right_edge   = wm_windows[i].x + wm_windows[i].width;
                } else if (point_in_titlebar(&wm_windows[i], x, y)) {
                    if (point_in_close_btn(&wm_windows[i], x, y)) {
                        /* Close button — hide window and transfer focus */
                        wm_close_window(i);
                    } else {
                        /* Start move drag on title bar */
                        drag_active  = 1;
                        drag_mode    = 1;
                        drag_win_idx = i;
                        drag_off_x   = x - wm_windows[i].x;
                        drag_off_y   = y - wm_windows[i].y;
                    }
                } else {
                    /* Client-area click → route to the app */
                    handle_client_click(&wm_windows[i], i, x, y);
                }
                break;
            }
        }
    }

    /* ---- Drag update ---- */
    if (drag_active && left_now) {
        wm_window_t *w = &wm_windows[drag_win_idx];
        if (drag_mode == 1) {
            /* Move */
            w->x = x - drag_off_x;
            w->y = y - drag_off_y;
            if (w->x < 0)                w->x = 0;
            if (w->y < UI_MENUBAR_H)     w->y = UI_MENUBAR_H;
            if (w->x + w->width  > scr_w) w->x = scr_w - w->width;
            if (w->y + w->height > scr_h)  w->y = scr_h - w->height;
        } else if (drag_mode == 2) {
            /* Resize bottom-right: top-left corner fixed */
            int new_w = (x + drag_off_x) - w->x;
            int new_h = (y + drag_off_y) - w->y;
            if (new_w < WIN_MIN_W) new_w = WIN_MIN_W;
            if (new_h < WIN_MIN_H) new_h = WIN_MIN_H;
            if (w->x + new_w > scr_w) new_w = scr_w - w->x;
            if (w->y + new_h > scr_h)  new_h = scr_h - w->y;
            w->width  = new_w;
            w->height = new_h;
        } else if (drag_mode == 3) {
            /* Resize bottom-left: right edge and top fixed */
            int new_x = x - drag_off_x;
            int new_h = (y + drag_off_y) - w->y;
            int new_w = drag_right_edge - new_x;
            if (new_w < WIN_MIN_W) { new_x = drag_right_edge - WIN_MIN_W; new_w = WIN_MIN_W; }
            if (new_x < 0)        { new_x = 0; new_w = drag_right_edge; }
            if (new_h < WIN_MIN_H) new_h = WIN_MIN_H;
            if (w->y + new_h > scr_h)  new_h = scr_h - w->y;
            w->x      = new_x;
            w->width  = new_w;
            w->height = new_h;
        }
    }

    /* ---- Button released → end drag ---- */
    if (!left_now && left_prev) { drag_active = 0; drag_mode = 0; }

    /* ---- Route mouse position to active KAPP (e.g. Paint dragging) ---- */
    {
        wm_window_t *aw = &wm_windows[wm_active];
        if (!aw->hidden && aw->type == WM_TYPE_KAPP) {
            int rx = x - (aw->x + WM_BORDER);
            int ry = y - (aw->y + WM_TITLEBAR_H);
            kapp_handle_mouse(aw->instance, wm_active, rx, ry, left_now);
        }
    }

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
    /* 1. Wallpaper (scaled) */
    fb_blit_scaled(0, 0, scr_w, scr_h, wm_wallpaper, WP_W, WP_H);

    /* 2. Green tint overlay — transforms the blue wallpaper toward cyberpunk green */
    fb_fill_rect_alpha(0, 0, scr_w, scr_h, COL_WALLPAPER_TINT, 160);

    /* 3. Vignette — darken screen edges for depth */
    fb_vignette((uint32_t)scr_w, (uint32_t)scr_h, 80);

    /* 4. Subtle wallpaper dimming behind the focused window */
    if (wm_active >= 0 && wm_active < WM_MAX_WINDOWS &&
        !wm_windows[wm_active].hidden) {
        wm_window_t *aw = &wm_windows[wm_active];
        /* Darken everything OUTSIDE the active window slightly */
        fb_fill_rect_alpha(0,        0,        scr_w,      aw->y,      0x000000, 30);
        fb_fill_rect_alpha(0,        aw->y+aw->height, scr_w,
                           scr_h - aw->y - aw->height,     0x000000, 30);
        fb_fill_rect_alpha(0,        aw->y,    aw->x,      aw->height, 0x000000, 30);
        fb_fill_rect_alpha(aw->x+aw->width, aw->y,
                           scr_w - aw->x - aw->width, aw->height,  0x000000, 30);
    }

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
            } else if (w->type == WM_TYPE_USER) {
                if (w->pixels) {
                    int cx = w->x + WM_BORDER;
                    int cy = w->y + WM_TITLEBAR_H;
                    int cw = w->width  - 2 * WM_BORDER;
                    int ch = w->height - WM_TITLEBAR_H - WM_BORDER;
                    if (cw == w->pix_w && ch == w->pix_h) {
                        fb_blit_pixels(cx, cy, w->pixels, cw, ch);
                    } else {
                        /* Window was resized: scale pixel content to fit. */
                        fb_blit_scaled(cx, cy, cw, ch,
                                       w->pixels, w->pix_w, w->pix_h);
                    }
                }
            } else if (w->type == WM_TYPE_KAPP) {
                int cx = w->x + WM_BORDER;
                int cy = w->y + WM_TITLEBAR_H;
                int cw = w->width  - 2 * WM_BORDER;
                int ch = w->height - WM_TITLEBAR_H - WM_BORDER;
                kapp_render_window(w->instance, i, cx, cy, cw, ch);
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

    /*
     * The kernel's built-in menubar and dock chrome are intentionally NOT
     * drawn here.  desktop.elf provides its own menu bar and dock as USER
     * windows, giving richer interaction and a user-space-managed layout.
     * Drawing both would produce overlapping chrome.
     *
     * The launcher ("Apps" dropdown, top-left) is kept as a fallback for
     * sessions where desktop.elf is not running.
     */
    kapp_tick_all();
    draw_launcher();
    draw_cursor(mouse_get_x(), mouse_get_y());
    fb_flush();
}

/* ================================================================
 * Public API
 * ================================================================ */

void wm_init(int sw, int sh) {
    int i;
    scr_w         = sw;
    scr_h         = sh;
    launcher_open = 0;
    kapp_init();

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

    /* Spawn the initial terminal — desktop.elf is no longer auto-run. */
    wm_spawn(WM_TYPE_TERMINAL);

    wm_draw_all();
}

void wm_spawn_terminal(void) {
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

    if (w->x < 0)                 w->x = 0;
    if (w->y < UI_MENUBAR_H)      w->y = UI_MENUBAR_H;
    if (w->x + w->width  > scr_w) w->x = scr_w - w->width;
    if (w->y + w->height > scr_h)  w->y = scr_h - w->height;

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

/* ================================================================
 * User-space WM — syscalls 20-26
 *
 * User windows live in wm_windows[] as WM_TYPE_USER entries,
 * giving them automatic chrome (title bar, close button, border),
 * mouse dragging, and z-order rendering for free.
 *
 * wid returned by SYS_WM_CREATE is the index into wm_windows[].
 * ================================================================ */

#define USER_WM_ADDR_MIN 0x300000UL

/* Route keyboard scancode to the focused USER window's event queue. */
void wm_push_key(uint8_t scancode) {
    wm_window_t *w = &wm_windows[wm_active];
    if (w->hidden || w->type != WM_TYPE_USER) return;
    wm_event_t e;
    e.type = (scancode & 0x80u) ? 2u : 1u;
    e.wid  = (uint16_t)wm_active;
    e.x    = (int16_t)scancode;
    e.y    = 0;
    e.btn  = 0;
    /* Phase 5: log which slot owns the focused window (key routing target) */
    if (!(scancode & 0x80u)) {  /* key-down only */
        serial_write(COM1, "[WM] key sc=0x");
        serial_write_hex(COM1, scancode);
        serial_write(COM1, " -> owner_slot=");
        serial_write_dec(COM1, (uint32_t)w->owner_slot);
        serial_write(COM1, " active_wid=");
        serial_write_dec(COM1, (uint32_t)wm_active);
        serial_write(COM1, " caller_proc=");
        serial_write_dec(COM1, (uint32_t)current_proc);
        serial_write(COM1, "\n");
    }
    wm_push_to_slot(w->owner_slot, e);
}

/* Route mouse event to the focused USER window's event queue.
 * Converts screen coords to client-area relative (0,0 = content top-left),
 * then inverse-scales to pixel-buffer coords if the window has been resized. */
void wm_push_mouse_event(int x, int y, uint8_t buttons, uint8_t prev) {
    wm_window_t *w = &wm_windows[wm_active];
    if (w->hidden || w->type != WM_TYPE_USER) return;
    int rx = x - (w->x + WM_BORDER);
    int ry = y - (w->y + WM_TITLEBAR_H);
    /* Map from current display size back to the pixel buffer coordinate space. */
    int cw = w->width  - 2 * WM_BORDER;
    int ch = w->height - WM_TITLEBAR_H - WM_BORDER;
    if (cw > 0 && ch > 0 && w->pix_w > 0 && w->pix_h > 0) {
        rx = rx * w->pix_w / cw;
        ry = ry * w->pix_h / ch;
    }
    wm_event_t e;
    e.wid  = (uint16_t)wm_active;
    e.x    = (int16_t)rx;
    e.y    = (int16_t)ry;
    e.btn  = buttons;
    e.type = (buttons != prev) ? 4u : 3u;
    wm_push_to_slot(w->owner_slot, e);
}

/* Drain available PS/2 bytes non-blocking — called at SYS_WM_EVENT time. */
static void wm_pump_input(void) {
    /*
     * Drain any bytes sitting in the PS/2 output buffer.
     *
     * In QEMU (and some real hardware) a polling read via inb(0x60) can race
     * ahead of the IRQ1 delivery: the polling path wins the byte before the
     * interrupt handler fires, so keyboard_irq_handler never sees it and
     * wm_push_key is never called.  We must therefore call wm_push_key here
     * for every keyboard byte we read, just as the IRQ handler does.
     * Mouse bytes (MOBF set) go to mouse_handle_byte as before.
     */
    for (int i = 0; i < 32; i++) {
        uint8_t st = inb(0x64);
        if (!(st & 0x01u)) break;   /* output buffer empty */
        uint8_t data = inb(0x60);
        if (st & 0x20u) {
            mouse_handle_byte(data);
        } else {
            /* Keyboard byte: inject as if it came from IRQ1 (push to ring
             * buffer, route to focused USER window slot queue, wake waiters). */
            keyboard_inject_scancode(data);
        }
    }
}

static int uw_valid(int wid) {
    return wid >= 0 && wid < WM_MAX_WINDOWS &&
           !wm_windows[wid].hidden &&
           wm_windows[wid].type == WM_TYPE_USER;
}

/* ================================================================
 * wm_syscall — dispatched from idt.c for eax = 20..26
 *
 *   nr = eax   a = ecx   b = edx   c = ebx
 * ================================================================ */
int32_t wm_syscall(uint32_t nr, uint32_t a, uint32_t b, uint32_t c) {
    switch (nr) {

    /* ----------------------------------------------------------
     * SYS_WM_CREATE (20)
     *   a = x,  b = y,  c = (content_w << 16) | content_h
     * content_w × content_h is the drawable client area.
     * Returns wid (index into wm_windows[]) or negative errno.
     * ---------------------------------------------------------- */
    case 20: {
        int wx = (int)(int16_t)(a & 0xFFFFu);
        int wy = (int)(int16_t)(b & 0xFFFFu);
        int cw = (int)((c >> 16) & 0xFFFFu);
        int ch = (int)(c & 0xFFFFu);

        if (cw <= 0 || ch <= 0 || cw > 800 || ch > 600)
            return -(int32_t)22;   /* -EINVAL */

        /* Find a free slot (hidden = 1 means free) */
        int slot = -1;
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (wm_windows[i].hidden) { slot = i; break; }
        }
        if (slot < 0) return -(int32_t)12;   /* -ENOMEM */

        /* Allocate pixel backing store for the client area only */
        uint32_t npix = (uint32_t)cw * (uint32_t)ch;
        uint32_t bytes_needed = npix * 4u;
        serial_write(COM1, "[WM] CREATE pixbuf need=");
        serial_write_hex(COM1, bytes_needed);
        serial_write(COM1, " pool_used=");
        serial_write_hex(COM1, pixbuf_pool_ptr - PIXBUF_POOL_BASE);
        serial_write(COM1, " pool_total=");
        serial_write_hex(COM1, PIXBUF_POOL_LIMIT - PIXBUF_POOL_BASE);
        serial_write(COM1, "\n");
        uint32_t *buf = pixbuf_alloc(bytes_needed);
        if (!buf) {
            serial_write(COM1, "[WM] CREATE FAIL: pixbuf_alloc returned NULL (pool exhausted)\n");
            return -(int32_t)12;
        }
        for (uint32_t i = 0; i < npix; i++) buf[i] = 0;

        wm_window_t *w = &wm_windows[slot];
        w->x          = wx;
        w->y          = wy;
        w->width      = cw + 2 * WM_BORDER;
        w->height     = ch + WM_TITLEBAR_H + WM_BORDER;
        w->type       = WM_TYPE_USER;
        w->instance   = 0;
        w->hidden     = 0;
        w->pixels     = buf;
        w->pix_w      = cw;
        w->pix_h      = ch;
        w->owner_slot = (current_proc >= 0) ? current_proc : -1;
        w->anim_alpha = 0;   /* fade in on first draw */
        strncpy(w->title_buf, "App", 31);
        w->title_buf[31] = '\0';
        w->title    = w->title_buf;

        serial_write(COM1, "[WM] CREATE wid=");
        serial_write_dec(COM1, (uint32_t)slot);
        serial_write(COM1, " owner_slot=");
        serial_write_dec(COM1, (uint32_t)w->owner_slot);
        serial_write(COM1, " cw=");
        serial_write_dec(COM1, (uint32_t)cw);
        serial_write(COM1, " ch=");
        serial_write_dec(COM1, (uint32_t)ch);
        serial_write(COM1, "\n");
        proc_dump_table("SYS_WM_CREATE");

        wm_set_active(slot);
        wm_draw_all();
        return (int32_t)slot;
    }

    /* ----------------------------------------------------------
     * SYS_WM_DESTROY (21)
     *   a = wid
     * Hides the window and frees its slot.  Returns 0 or -EBADF.
     *
     * NOTE: we do NOT call wm_close_window() here, because that
     * function pushes a WM_EV_CLOSE event to wm_eq for WM_TYPE_USER
     * windows.  When the app itself calls wm_destroy() it already
     * knows it is exiting — the event is unnecessary.  Worse, it
     * poisons wm_eq: the next WM program dequeues the stale CLOSE
     * event as its very first event and immediately quits.
     *
     * wm_close_window() is still used from the mouse-handler path
     * (close-button click) where pushing CLOSE is the correct
     * mechanism to notify the app that the user dismissed the window.
     * ---------------------------------------------------------- */
    case 21: {
        int wid = (int)a;
        if (!uw_valid(wid)) return -(int32_t)9;   /* -EBADF */
        {
            wm_window_t *w = &wm_windows[wid];
            /* Return pixel backing store to the pool */
            pixbuf_free(w->pixels);
            w->pixels = (uint32_t *)0;
            w->hidden = 1;

            /* Cancel any active drag on this window */
            if (drag_win_idx == wid) { drag_active = 0; drag_win_idx = -1; }

            /* Transfer focus to another visible window, if one exists */
            if (wm_active == wid) {
                int found = -1;
                for (int i = 0; i < WM_MAX_WINDOWS; i++)
                    if (!wm_windows[i].hidden) { found = i; break; }
                if (found >= 0)
                    wm_set_active(found);
            }
        }
        wm_draw_all();
        return 0;
    }

    /* ----------------------------------------------------------
     * SYS_WM_BLIT (22)
     *   a = wid,  b = user_pixel_buf (32bpp, cw*ch*4 bytes),  c = len
     * Copies pixels into the backing store then repaints.
     * Returns 0 or negative errno.
     * ---------------------------------------------------------- */
    case 22: {
        int wid = (int)a;
        if (!uw_valid(wid))          return -(int32_t)9;    /* -EBADF  */
        if (b < USER_WM_ADDR_MIN)    return -(int32_t)22;   /* -EINVAL */
        wm_window_t *w = &wm_windows[wid];
        /* Always use the original buffer dimensions (pix_w × pix_h).
         * w->width/height may have changed if the user resized the window,
         * but the backing store and the ELF's frame buffer are still pix_w×pix_h. */
        uint32_t npix     = (uint32_t)w->pix_w * (uint32_t)w->pix_h;
        uint32_t expected = npix * 4u;
        if (c < expected)            return -(int32_t)22;

        const uint32_t *src = (const uint32_t *)b;
        for (uint32_t i = 0; i < npix; i++) w->pixels[i] = src[i];

        /*
         * Fast path: when a topmost (active) window blits, repaint ONLY its
         * client area to the back buffer and flush.  Skip the full-scene
         * composite (wallpaper + every other window + chrome + kapps +
         * launcher + cursor) that wm_draw_all() does.
         *
         * Why this matters: every GUI ELF animation tick calls SYS_WM_BLIT.
         * The old code ran wm_draw_all() per blit, so N concurrent animating
         * apps each paid the cost of recompositing the other N-1 apps.
         * That made performance scale as N², which is why "even one other
         * ELF" made everything unusable.
         *
         * Correctness: the active window is always drawn last (pass 1) in
         * wm_draw_all, so it's topmost — pasting its pixels straight into
         * the back buffer can't be obscured by another window.  Chrome
         * around the client rect is unchanged from the last full repaint
         * and doesn't need redrawing on every blit.  Non-active windows
         * fall back to wm_draw_all to preserve z-order correctness.
         */
        if (!w->hidden) {
            int cx = w->x + WM_BORDER;
            int cy = w->y + WM_TITLEBAR_H;
            int cw = w->width  - 2 * WM_BORDER;
            int ch = w->height - WM_TITLEBAR_H - WM_BORDER;
            if (cw == w->pix_w && ch == w->pix_h)
                fb_blit_pixels(cx, cy, w->pixels, cw, ch);
            else
                fb_blit_scaled(cx, cy, cw, ch, w->pixels, w->pix_w, w->pix_h);

            /*
             * Z-order preservation: if a non-active window just painted into
             * an area where the active window also lives, re-blit the active
             * window's client area on top so it stays visually topmost.
             * O(2) work per blit instead of O(N) over all windows.
             */
            if ((int)wid != wm_active && wm_active >= 0 &&
                wm_active < WM_MAX_WINDOWS && !wm_windows[wm_active].hidden) {
                wm_window_t *a = &wm_windows[wm_active];
                if (a->type == WM_TYPE_USER && a->pixels) {
                    int acx = a->x + WM_BORDER;
                    int acy = a->y + WM_TITLEBAR_H;
                    int acw = a->width  - 2 * WM_BORDER;
                    int ach = a->height - WM_TITLEBAR_H - WM_BORDER;
                    /* Cheap overlap check */
                    int ox0 = (cx > acx) ? cx : acx;
                    int oy0 = (cy > acy) ? cy : acy;
                    int ox1 = (cx + cw < acx + acw) ? cx + cw : acx + acw;
                    int oy1 = (cy + ch < acy + ach) ? cy + ch : acy + ach;
                    if (ox0 < ox1 && oy0 < oy1) {
                        if (acw == a->pix_w && ach == a->pix_h)
                            fb_blit_pixels(acx, acy, a->pixels, acw, ach);
                        else
                            fb_blit_scaled(acx, acy, acw, ach,
                                           a->pixels, a->pix_w, a->pix_h);
                    }
                }
            }

            draw_cursor(mouse_get_x(), mouse_get_y());
            fb_flush();
            return 0;
        }

        wm_draw_all();
        return 0;
    }

    /* ----------------------------------------------------------
     * SYS_WM_MOVE (23)
     *   a = wid,  b = new_x,  c = new_y
     * Programmatically repositions the window and repaints.
     * Returns 0 or -EBADF.
     * ---------------------------------------------------------- */
    case 23: {
        int wid = (int)a;
        if (!uw_valid(wid)) return -(int32_t)9;
        wm_windows[wid].x = (int)(int16_t)(b & 0xFFFFu);
        wm_windows[wid].y = (int)(int16_t)(c & 0xFFFFu);
        wm_draw_all();
        return 0;
    }

    /* ----------------------------------------------------------
     * SYS_WM_EVENT (24)
     *   a = ptr to wm_event_t,  b = sizeof buffer
     * Drains PS/2 input, then dequeues one event.
     * Returns event type (> 0) or 0 if queue empty.  Non-blocking.
     * ---------------------------------------------------------- */
    case 24: {
        if (a < USER_WM_ADDR_MIN)    return -(int32_t)22;
        if (b < sizeof(wm_event_t))  return -(int32_t)22;

        wm_pump_input();

        int s = (current_proc >= 0) ? current_proc : 0;
        if (slot_eq_head[s] == slot_eq_tail[s]) return 0;   /* queue empty */

        wm_event_t *dst = (wm_event_t *)a;
        *dst = slot_eq[s][slot_eq_head[s]];
        slot_eq_head[s] = (uint8_t)((slot_eq_head[s] + 1u) % UW_EQ_SIZE);
        return (int32_t)dst->type;
    }

    /* ----------------------------------------------------------
     * SYS_WM_FLUSH (25)
     *   a = wid  (-1 = full repaint)
     * Composites everything to the physical framebuffer.
     * Returns 0.
     * ---------------------------------------------------------- */
    case 25: {
        wm_draw_all();
        return 0;
    }

    /* ----------------------------------------------------------
     * SYS_WM_SETFOCUS (26)
     *   a = wid
     * Gives the window keyboard focus.  Returns 0 or -EBADF.
     * ---------------------------------------------------------- */
    case 26: {
        int wid = (int)a;
        if (!uw_valid(wid)) return -(int32_t)9;
        wm_set_active(wid);
        wm_draw_all();
        return 0;
    }

    default:
        return -(int32_t)22;
    }
}

/* ================================================================
 * wm_cleanup_for_slot / wm_cleanup_all_user_windows
 *
 * Called from proc_exit() and proc_register_initial() to tear down
 * any USER windows owned by a dying process slot.
 * ================================================================ */
void wm_cleanup_for_slot(int slot) {
    int focus_gone = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *w = &wm_windows[i];
        if (!w->hidden && w->type == WM_TYPE_USER && w->owner_slot == slot) {
            pixbuf_free(w->pixels);
            w->pixels = (uint32_t *)0;
            w->hidden = 1;
            if (drag_win_idx == i) { drag_active = 0; drag_win_idx = -1; }
            if (wm_active == i) focus_gone = 1;
        }
    }
    /* Flush the dead process's event queue */
    wm_flush_slot_queue(slot);
    /* Restore focus to another visible window */
    if (focus_gone) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (!wm_windows[i].hidden) { wm_set_active(i); break; }
        }
    }

    /* Diagnostics: remaining active USER windows and pool usage */
    int active_user = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (!wm_windows[i].hidden && wm_windows[i].type == WM_TYPE_USER)
            active_user++;
    serial_write(COM1, "[WM] cleanup slot=");
    serial_write_dec(COM1, (uint32_t)slot);
    serial_write(COM1, " remaining_user_wins=");
    serial_write_dec(COM1, (uint32_t)active_user);
    serial_write(COM1, " pixbuf_used=");
    serial_write_hex(COM1, pixbuf_pool_ptr - PIXBUF_POOL_BASE);
    serial_write(COM1, "/");
    serial_write_hex(COM1, PIXBUF_POOL_LIMIT - PIXBUF_POOL_BASE);
    serial_write(COM1, "\n");
}

void wm_cleanup_all_user_windows(void) {
    int focus_gone = 0;
    int cleaned = 0;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm_window_t *w = &wm_windows[i];
        if (!w->hidden && w->type == WM_TYPE_USER) {
            /* No individual pixbuf_free needed — pixbuf_reset() below is authoritative */
            w->pixels = (uint32_t *)0;
            w->hidden = 1;
            if (drag_win_idx == i) { drag_active = 0; drag_win_idx = -1; }
            if (wm_active == i) focus_gone = 1;
            cleaned++;
        }
    }
    serial_write(COM1, "[WM] cleanup_all: removed ");
    serial_write_dec(COM1, (uint32_t)cleaned);
    serial_write(COM1, " user window(s)\n");
    /* Reset the pixel buffer pool — all USER windows are now gone */
    pixbuf_reset();
    /* Flush all per-slot queues */
    for (int s = 0; s < MAX_PROCS; s++)
        wm_flush_slot_queue(s);
    /* Restore focus */
    if (focus_gone) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (!wm_windows[i].hidden) { wm_set_active(i); break; }
        }
    }
}

/* ================================================================
 * wm_spawn_kapp — open a ring-0 kernel application window.
 * If the kapp is already open, just focus it.
 * ================================================================ */
void wm_spawn_kapp(int kapp_id) {
    if (kapp_id < 0 || kapp_id >= NUM_KAPPS) return;

    /* Already open? Just focus it. */
    if (kapp_is_open(kapp_id)) {
        int wi = kapp_window_index(kapp_id);
        if (wi >= 0 && !wm_windows[wi].hidden) {
            wm_set_active(wi);
            wm_draw_all();
            return;
        }
    }

    /* Find a free window slot */
    int wi = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (wm_windows[i].hidden) { wi = i; break; }
    if (wi < 0) return;

    const kapp_def_t *def = &kapp_defs[kapp_id];
    wm_window_t *w = &wm_windows[wi];

    /* Position: centered on screen (within desktop usable area) */
    int avail_y = UI_MENUBAR_H;
    int avail_h = scr_h - UI_DOCK_H - UI_MENUBAR_H;
    w->x          = (scr_w - def->def_w) / 2;
    w->y          = avail_y + (avail_h - def->def_h) / 2;
    w->width      = def->def_w;
    w->height     = def->def_h;
    w->type       = WM_TYPE_KAPP;
    w->instance   = kapp_id;
    w->hidden     = 0;
    w->pixels     = (uint32_t *)0;
    w->owner_slot = -1;
    w->anim_alpha = 0;   /* fade in on first draw */

    /* Use the kapp title */
    strncpy(w->title_buf, def->title, 31);
    w->title_buf[31] = '\0';
    w->title = w->title_buf;

    /* Notify the kapp framework */
    kapp_open(kapp_id, wi);

    wm_set_active(wi);
    wm_draw_all();
}

void wm_close_kapp(int wi) {
    if (wi < 0 || wi >= WM_MAX_WINDOWS) return;
    if (wm_windows[wi].hidden) return;
    if (wm_windows[wi].type != WM_TYPE_KAPP) return;
    wm_close_window(wi);
    wm_draw_all();
}

int wm_active_is_kapp(void) {
    if (wm_windows[wm_active].hidden) return 0;
    return wm_windows[wm_active].type == WM_TYPE_KAPP;
}

void wm_kapp_handle_key(int key_type, char ch) {
    if (!wm_active_is_kapp()) return;
    wm_window_t *w = &wm_windows[wm_active];
    kapp_handle_key(w->instance, wm_active, key_type, ch);
    wm_draw_all();
}

/* ================================================================
 * wm_set_fs — give the WM a reference to the mounted FAT16 FS.
 * Called by shell_run() after syscall_set_fs().  Enables "Run App..."
 * in the Apps launcher dropdown.
 * ================================================================ */
void wm_set_fs(fat16_fs_t *fs) {
    wm_fs = fs;
    kapp_set_fs(fs);   /* also propagate to kapp framework */
}
