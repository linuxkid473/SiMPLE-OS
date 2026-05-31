#ifndef SIMPLE_KAPP_H
#define SIMPLE_KAPP_H

#include "types.h"
#include "fat16.h"
#include "vga.h"
#include "keyboard.h"

/* ---- Kapp type IDs ---- */
#define KAPP_CLOCK    0
#define KAPP_ABOUT    1
#define KAPP_SYSINFO  2
#define KAPP_TASKMGR  3
#define KAPP_PAINT    4
#define KAPP_NOTEPAD  5
#define KAPP_FILEMGR  6
#define KAPP_FILEVIEW 7
#define KAPP_SETTINGS 8
#define KAPP_SNAKE    9
#define KAPP_BREAKOUT 10
#define KAPP_PONG     11
#define KAPP_2048     12
#define KAPP_SPEEDWAY      13
#define KAPP_CONSTITUTION  14
#define NUM_KAPPS          15

/* ---- Color palette ---- */
#define KA_BG      0x080808U
#define KA_TEXT    0xAAFFBBU
#define KA_DIM     0x336644U
#define KA_BRIGHT  0x00FF88U
#define KA_RED     0xFF4444U
#define KA_YELLOW  0xFFDD44U
#define KA_BTNBG   0x0A2015U
#define KA_BTNBD   0x33AA55U
#define KA_BTNFG   0xCCFFCCU
#define KA_HEADBG  0x001A08U
#define KA_HEADFG  0x44FF88U
#define KA_SELBG   0x0A3A1AU
#define KA_SELFG   0xFFFFFFU
#define KA_BORDER  0x1A3322U
#define KA_WHITE   0xFFFFFFU
#define KA_ORANGE  0xFF8844U

/* ---- App framework ---- */
typedef struct {
    const char *title;
    int         def_w, def_h;
    void (*create)(int wi);
    void (*destroy)(int wi);
    void (*render)(int wi, int cx, int cy, int cw, int ch);
    void (*key)(int wi, int key_type, char ch);
    void (*click)(int wi, int client_x, int client_y);
    void (*mouse)(int wi, int client_x, int client_y, int btn_held);
    void (*tick)(int wi);
} kapp_def_t;

extern const kapp_def_t kapp_defs[NUM_KAPPS];

/* ---- Drawing helpers (write to framebuffer) ---- */
void kd_fill(int x, int y, int w, int h, uint32_t col);
void kd_hline(int x, int y, int w, uint32_t col);
void kd_vline(int x, int y, int h, uint32_t col);
void kd_rect(int x, int y, int w, int h, uint32_t col);
void kd_str(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void kd_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void kd_button(int x, int y, int w, int h, const char *label, int active);
void kd_itoa(int32_t v, char *buf, int cap);
void kd_utoa(uint32_t v, char *buf, int cap);
void kd_hex(uint32_t v, char *buf, int cap);
void kd_str_n(int x, int y, const char *s, int max_chars, uint32_t fg, uint32_t bg);

/* ---- Dispatch (called from wm.c) ---- */
void kapp_init(void);
void kapp_open(int kapp_id, int wi);
void kapp_close(int kapp_id, int wi);
void kapp_render_window(int kapp_id, int wi, int cx, int cy, int cw, int ch);
void kapp_handle_key(int kapp_id, int wi, int key_type, char ch);
void kapp_handle_click(int kapp_id, int wi, int client_x, int client_y);
void kapp_handle_mouse(int kapp_id, int wi, int client_x, int client_y, int btn_held);
void kapp_tick_all(void);
int  kapp_is_open(int kapp_id);
int  kapp_any_open(void);
int  kapp_window_index(int kapp_id);

/* FAT16 access shared among kapps */
void        kapp_set_fs(fat16_fs_t *fs);
fat16_fs_t *kapp_get_fs(void);

#endif
