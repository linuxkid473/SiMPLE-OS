#include "kapp.h"
#include "vga.h"
#include "string.h"

/* ================================================================
 * Drawing helpers — write directly to the framebuffer.
 * Wrappers around fb_fill_rect / fb_draw_string_px.
 * ================================================================ */

void kd_fill(int x, int y, int w, int h, uint32_t col) {
    fb_fill_rect(x, y, w, h, col);
}

void kd_hline(int x, int y, int w, uint32_t col) {
    fb_fill_rect(x, y, w, 1, col);
}

void kd_vline(int x, int y, int h, uint32_t col) {
    fb_fill_rect(x, y, 1, h, col);
}

void kd_rect(int x, int y, int w, int h, uint32_t col) {
    fb_fill_rect(x,         y,         w, 1, col);
    fb_fill_rect(x,         y + h - 1, w, 1, col);
    fb_fill_rect(x,         y,         1, h, col);
    fb_fill_rect(x + w - 1, y,         1, h, col);
}

void kd_str(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    fb_draw_string_px(x, y, s, fg, bg);
}

void kd_str_n(int x, int y, const char *s, int max_chars,
              uint32_t fg, uint32_t bg) {
    char tmp[128];
    int i = 0;
    while (s[i] && i < max_chars && i < 127) { tmp[i] = s[i]; i++; }
    tmp[i] = '\0';
    fb_draw_string_px(x, y, tmp, fg, bg);
}

void kd_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    char buf[2] = { c, '\0' };
    fb_draw_string_px(x, y, buf, fg, bg);
}

void kd_button(int x, int y, int w, int h, const char *label, int active) {
    uint32_t bg = active ? 0x0F3A20U : KA_BTNBG;
    kd_fill(x, y, w, h, bg);
    kd_fill(x, y, w, 2, active ? 0x55FF88U : 0x1A4A28U);  /* gloss */
    kd_hline(x,     y,         w, KA_BTNBD);               /* top edge */
    kd_vline(x,     y,         h, KA_BTNBD);               /* left edge */
    kd_hline(x,     y + h - 1, w, 0x051008U);             /* bot shadow */
    kd_vline(x + w - 1, y,     h, 0x051008U);             /* right shadow */
    /* label centred */
    int llen = (int)strlen(label);
    int lx   = x + (w - llen * 8) / 2;
    int ly   = y + (h - 8) / 2;
    fb_draw_string_px(lx, ly, label, KA_BTNFG, bg);
}

/* Convert signed integer to decimal string */
void kd_itoa(int32_t v, char *buf, int cap) {
    if (cap <= 1) { if (cap == 1) buf[0] = '\0'; return; }
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int pos = 0;
    int neg = (v < 0);
    uint32_t u = neg ? ((v == (int32_t)0x80000000) ? 2147483648U : (uint32_t)(-v))
                     : (uint32_t)v;
    while (u > 0 && pos < 11) { tmp[pos++] = (char)('0' + u % 10); u /= 10; }
    int out = 0;
    if (neg && out + 1 < cap) buf[out++] = '-';
    while (pos > 0 && out + 1 < cap) buf[out++] = tmp[--pos];
    buf[out] = '\0';
}

void kd_utoa(uint32_t v, char *buf, int cap) {
    if (cap <= 1) { if (cap == 1) buf[0] = '\0'; return; }
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int pos = 0;
    while (v > 0 && pos < 11) { tmp[pos++] = (char)('0' + v % 10); v /= 10; }
    int out = 0;
    while (pos > 0 && out + 1 < cap) buf[out++] = tmp[--pos];
    buf[out] = '\0';
}

void kd_hex(uint32_t v, char *buf, int cap) {
    static const char hex[] = "0123456789ABCDEF";
    if (cap < 3) { if (cap > 0) buf[0] = '\0'; return; }
    buf[0] = '0'; buf[1] = 'x';
    int out = 2;
    int started = 0;
    for (int i = 7; i >= 0 && out + 1 < cap; i--) {
        uint8_t nibble = (uint8_t)((v >> (i * 4)) & 0xFu);
        if (nibble || started || i == 0) {
            buf[out++] = hex[nibble];
            started = 1;
        }
    }
    buf[out] = '\0';
}

/* ================================================================
 * Kapp registry and dispatch
 * ================================================================ */

/* Forward-declare all kapp render/lifecycle functions */
void clock_create(int wi);   void clock_destroy(int wi);
void clock_render(int wi, int cx, int cy, int cw, int ch);
void clock_key(int wi, int kt, char ch);
void clock_click(int wi, int x, int y);
void clock_mouse(int wi, int x, int y, int btn);
void clock_tick(int wi);

void about_create(int wi);   void about_destroy(int wi);
void about_render(int wi, int cx, int cy, int cw, int ch);
void about_key(int wi, int kt, char ch);
void about_click(int wi, int x, int y);
void about_mouse(int wi, int x, int y, int btn);
void about_tick(int wi);

void sysinfo_create(int wi);  void sysinfo_destroy(int wi);
void sysinfo_render(int wi, int cx, int cy, int cw, int ch);
void sysinfo_key(int wi, int kt, char ch);
void sysinfo_click(int wi, int x, int y);
void sysinfo_mouse(int wi, int x, int y, int btn);
void sysinfo_tick(int wi);

void taskmgr_create(int wi);  void taskmgr_destroy(int wi);
void taskmgr_render(int wi, int cx, int cy, int cw, int ch);
void taskmgr_key(int wi, int kt, char ch);
void taskmgr_click(int wi, int x, int y);
void taskmgr_mouse(int wi, int x, int y, int btn);
void taskmgr_tick(int wi);

void paint_create(int wi);    void paint_destroy(int wi);
void paint_render(int wi, int cx, int cy, int cw, int ch);
void paint_key(int wi, int kt, char ch);
void paint_click(int wi, int x, int y);
void paint_mouse(int wi, int x, int y, int btn);
void paint_tick(int wi);

void notepad_create(int wi);  void notepad_destroy(int wi);
void notepad_render(int wi, int cx, int cy, int cw, int ch);
void notepad_key(int wi, int kt, char ch);
void notepad_click(int wi, int x, int y);
void notepad_mouse(int wi, int x, int y, int btn);
void notepad_tick(int wi);

void filemgr_create(int wi);  void filemgr_destroy(int wi);
void filemgr_render(int wi, int cx, int cy, int cw, int ch);
void filemgr_key(int wi, int kt, char ch);
void filemgr_click(int wi, int x, int y);
void filemgr_mouse(int wi, int x, int y, int btn);
void filemgr_tick(int wi);

void fileview_create(int wi); void fileview_destroy(int wi);
void fileview_render(int wi, int cx, int cy, int cw, int ch);
void fileview_key(int wi, int kt, char ch);
void fileview_click(int wi, int x, int y);
void fileview_mouse(int wi, int x, int y, int btn);
void fileview_tick(int wi);

void settings_create(int wi); void settings_destroy(int wi);
void settings_render(int wi, int cx, int cy, int cw, int ch);
void settings_key(int wi, int kt, char ch);
void settings_click(int wi, int x, int y);
void settings_mouse(int wi, int x, int y, int btn);
void settings_tick(int wi);

void snake_create(int wi);    void snake_destroy(int wi);
void snake_render(int wi, int cx, int cy, int cw, int ch);
void snake_key(int wi, int kt, char ch);
void snake_click(int wi, int x, int y);
void snake_mouse(int wi, int x, int y, int btn);
void snake_tick(int wi);

void breakout_create(int wi); void breakout_destroy(int wi);
void breakout_render(int wi, int cx, int cy, int cw, int ch);
void breakout_key(int wi, int kt, char ch);
void breakout_click(int wi, int x, int y);
void breakout_mouse(int wi, int x, int y, int btn);
void breakout_tick(int wi);

void pong_create(int wi);     void pong_destroy(int wi);
void pong_render(int wi, int cx, int cy, int cw, int ch);
void pong_key(int wi, int kt, char ch);
void pong_click(int wi, int x, int y);
void pong_mouse(int wi, int x, int y, int btn);
void pong_tick(int wi);

void g2048_create(int wi);    void g2048_destroy(int wi);
void g2048_render(int wi, int cx, int cy, int cw, int ch);
void g2048_key(int wi, int kt, char ch);
void g2048_click(int wi, int x, int y);
void g2048_mouse(int wi, int x, int y, int btn);
void g2048_tick(int wi);

void speedway_create(int wi);  void speedway_destroy(int wi);
void speedway_render(int wi, int cx, int cy, int cw, int ch);
void speedway_key(int wi, int kt, char ch);
void speedway_click(int wi, int x, int y);
void speedway_mouse(int wi, int x, int y, int btn);
void speedway_tick(int wi);

void constitution_create(int wi);  void constitution_destroy(int wi);
void constitution_render(int wi, int cx, int cy, int cw, int ch);
void constitution_key(int wi, int kt, char ch);
void constitution_click(int wi, int x, int y);
void constitution_mouse(int wi, int x, int y, int btn);
void constitution_tick(int wi);

/* Global kapp definitions table */
const kapp_def_t kapp_defs[NUM_KAPPS] = {
    [KAPP_CLOCK]    = { "Clock",        280, 160,
                        clock_create,    clock_destroy,    clock_render,
                        clock_key,       clock_click,      clock_mouse,    clock_tick },
    [KAPP_ABOUT]    = { "SiMPLE Racer", 400, 260,
                        about_create,    about_destroy,    about_render,
                        about_key,       about_click,      about_mouse,    about_tick },
    [KAPP_SYSINFO]  = { "System Info",  380, 300,
                        sysinfo_create,  sysinfo_destroy,  sysinfo_render,
                        sysinfo_key,     sysinfo_click,    sysinfo_mouse,  sysinfo_tick },
    [KAPP_TASKMGR]  = { "Task Manager", 460, 280,
                        taskmgr_create,  taskmgr_destroy,  taskmgr_render,
                        taskmgr_key,     taskmgr_click,    taskmgr_mouse,  taskmgr_tick },
    [KAPP_PAINT]    = { "Paint",        500, 320,
                        paint_create,    paint_destroy,    paint_render,
                        paint_key,       paint_click,      paint_mouse,    paint_tick },
    [KAPP_NOTEPAD]  = { "Notepad",      420, 300,
                        notepad_create,  notepad_destroy,  notepad_render,
                        notepad_key,     notepad_click,    notepad_mouse,  notepad_tick },
    [KAPP_FILEMGR]  = { "File Manager", 460, 340,
                        filemgr_create,  filemgr_destroy,  filemgr_render,
                        filemgr_key,     filemgr_click,    filemgr_mouse,  filemgr_tick },
    [KAPP_FILEVIEW] = { "File Viewer",  420, 300,
                        fileview_create, fileview_destroy, fileview_render,
                        fileview_key,    fileview_click,   fileview_mouse, fileview_tick },
    [KAPP_SETTINGS] = { "Settings",     340, 260,
                        settings_create, settings_destroy, settings_render,
                        settings_key,    settings_click,   settings_mouse, settings_tick },
    [KAPP_SNAKE]    = { "Snake",        296, 230,
                        snake_create,    snake_destroy,    snake_render,
                        snake_key,       snake_click,      snake_mouse,    snake_tick },
    [KAPP_BREAKOUT] = { "Breakout",     364, 262,
                        breakout_create, breakout_destroy, breakout_render,
                        breakout_key,    breakout_click,   breakout_mouse, breakout_tick },
    [KAPP_PONG]     = { "Pong",         368, 252,
                        pong_create,     pong_destroy,     pong_render,
                        pong_key,        pong_click,       pong_mouse,     pong_tick },
    [KAPP_2048]     = { "2048",         256, 296,
                        g2048_create,    g2048_destroy,    g2048_render,
                        g2048_key,       g2048_click,      g2048_mouse,    g2048_tick },
    [KAPP_SPEEDWAY]     = { "SiMPLE Speedway", 720, 460,
                            speedway_create,      speedway_destroy,      speedway_render,
                            speedway_key,         speedway_click,        speedway_mouse,      speedway_tick },
    [KAPP_CONSTITUTION] = { "Constitution",    560, 440,
                            constitution_create,  constitution_destroy,  constitution_render,
                            constitution_key,     constitution_click,    constitution_mouse,  constitution_tick },
};

/* ================================================================
 * Kapp instance tracking
 * ================================================================ */

static int kapp_win[NUM_KAPPS];  /* window index, -1 if not open */
static fat16_fs_t *g_kapp_fs = (fat16_fs_t *)0;

/* Must be called once during WM init to mark all kapps as closed */
void kapp_init(void) {
    for (int i = 0; i < NUM_KAPPS; i++) kapp_win[i] = -1;
}

void kapp_set_fs(fat16_fs_t *fs) { g_kapp_fs = fs; }
fat16_fs_t *kapp_get_fs(void)    { return g_kapp_fs; }

int kapp_is_open(int id) {
    if (id < 0 || id >= NUM_KAPPS) return 0;
    return kapp_win[id] >= 0;
}

int kapp_any_open(void) {
    for (int i = 0; i < NUM_KAPPS; i++)
        if (kapp_win[i] >= 0) return 1;
    return 0;
}

int kapp_window_index(int id) {
    if (id < 0 || id >= NUM_KAPPS) return -1;
    return kapp_win[id];
}

void kapp_open(int id, int wi) {
    if (id < 0 || id >= NUM_KAPPS) return;
    kapp_win[id] = wi;
    if (kapp_defs[id].create) kapp_defs[id].create(wi);
}

void kapp_close(int id, int wi) {
    if (id < 0 || id >= NUM_KAPPS) return;
    if (kapp_defs[id].destroy) kapp_defs[id].destroy(wi);
    kapp_win[id] = -1;
}

void kapp_render_window(int id, int wi, int cx, int cy, int cw, int ch) {
    if (id < 0 || id >= NUM_KAPPS) return;
    if (kapp_defs[id].render) kapp_defs[id].render(wi, cx, cy, cw, ch);
}

void kapp_handle_key(int id, int wi, int key_type, char ch) {
    if (id < 0 || id >= NUM_KAPPS) return;
    if (kapp_defs[id].key) kapp_defs[id].key(wi, key_type, ch);
}

void kapp_handle_click(int id, int wi, int client_x, int client_y) {
    if (id < 0 || id >= NUM_KAPPS) return;
    if (kapp_defs[id].click) kapp_defs[id].click(wi, client_x, client_y);
}

void kapp_handle_mouse(int id, int wi, int client_x, int client_y, int btn) {
    if (id < 0 || id >= NUM_KAPPS) return;
    if (kapp_defs[id].mouse) kapp_defs[id].mouse(wi, client_x, client_y, btn);
}

void kapp_tick_all(void) {
    for (int i = 0; i < NUM_KAPPS; i++) {
        if (kapp_win[i] >= 0 && kapp_defs[i].tick)
            kapp_defs[i].tick(kapp_win[i]);
    }
}
