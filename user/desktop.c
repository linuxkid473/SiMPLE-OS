/*
 * desktop.c — SiMPLE OS desktop shell + file browser
 *
 * Windows:
 *   menu_wid  : 800×24 menu bar  at screen (0, 0)
 *   dock_wid  : 800×52 dock      at screen (0, 548)
 *   browser_wid (optional): 240×320 file browser, opened by clicking FILES
 *
 * File browser:
 *   Opened by clicking the FILES dock button (toggle).
 *   Lists every .elf file on the root FAT16 partition.
 *   Click an entry → fork + exec that ELF.
 *   Close with the [X] title-bar button or ESC.
 *   Up/Down arrow keys scroll the list when there are more entries than fit.
 *
 * Pixel buffers for menu bar and dock are sbrk-allocated.
 * The browser's sbrk buffer is also allocated at startup and reused
 * across open/close cycles (kernel's WM kmalloc pool is conserved).
 */

#include "wm.h"
#include <dirent.h>

/* ---- libc / syscall wrappers ---- */
void exit(int code);
int  sbrk(int n);
int  wm_create(int x, int y, int w, int h);
int  wm_destroy(int wid);
int  wm_blit(int wid, unsigned int *buf, int len);
int  wm_event(wm_event_t *ev, int max);
int  wm_setfocus(int wid);
int  fork(void);
int  exec(const char *path);
int  yield(void);

/* ================================================================
 * Screen geometry
 * ================================================================ */
#define SCR_H   600
#define SCR_W   800

/* Menu bar window */
#define MB_W    SCR_W
#define MB_H    24
#define MB_WX   (-2)
#define MB_WY   (-18)   /* client y = MB_WY + 18 = 0 */

/* Dock window */
#define DK_W    SCR_W
#define DK_H    52
#define DK_WX   (-2)
#define DK_WY   (SCR_H - DK_H - 18)   /* client y = SCR_H - DK_H = 548 */

/* File browser window (client area 240 × 320) */
#define BR_W      240
#define BR_H      320
#define BR_WX     (SCR_W/2 - BR_W/2 - 2)          /* ≈ 278 */
#define BR_WY     (SCR_H/2 - BR_H/2 - 18)          /* ≈ 122 */
#define BR_HDR_H  22    /* header bar height                         */
#define BR_ENTRY_H 16   /* height of each file entry                 */
#define BR_VISIBLE  ((BR_H - BR_HDR_H) / BR_ENTRY_H)   /* 18 entries */
#define MAX_ELF    64   /* max ELF files to list                     */

/* ================================================================
 * Colors
 * ================================================================ */
/* menu bar / dock */
#define C_MB_BG     0x1C1C2C
#define C_DK_BG     0x16162A
#define C_DK_BTN    0x252540
#define C_DK_HOV    0x3A3A60
#define C_DK_BDR    0x4A4A80
#define C_TEXT_W    0xF0F0F0
#define C_TEXT_C    0x88CCFF
#define C_TEXT_DIM  0x888899
#define C_SEP       0x2A2A44

/* browser */
#define C_BR_BG     0x111120
#define C_BR_HDR    0x1A2048
#define C_BR_ENTRY  0x13131E
#define C_BR_HOV    0x22224A
#define C_BR_TXT    0xCCCCEE
#define C_BR_TXT_H  0x88CCFF
#define C_BR_SEP    0x1E1E38
#define C_BR_ICON   0x4488CC
#define C_BR_SCROLL 0x333366

/* ================================================================
 * 8x8 IBM VGA font (bit 0 = leftmost pixel)
 * ================================================================ */
static const unsigned char font8[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 00 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 20 space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 21 ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* 22 " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 23 # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 24 $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 25 % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 26 & */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 27 ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 28 ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 29 ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 2A * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 2B + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 2C , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 2D - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 2E . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 2F / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 30 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 31 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 32 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 33 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 34 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 35 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 36 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 37 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 38 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 39 9 */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 3A : */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 3B ; */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 3C < */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 3D = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 3E > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 3F ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 40 @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 41 A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 42 B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 43 C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 44 D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 45 E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 46 F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 47 G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 48 H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 49 I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 4A J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 4B K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 4C L */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 4D M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 4E N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 4F O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 50 P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 51 Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 52 R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 53 S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 54 T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 55 U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 56 V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 57 W */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 58 X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 59 Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 5A Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 5B [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 5C \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 5D ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 5E ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 5F _ */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 60 ` */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 61 a */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 62 b */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 63 c */
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, /* 64 d */
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, /* 65 e */
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, /* 66 f */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 67 g */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 68 h */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 69 i */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 6A j */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 6B k */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 6C l */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 6D m */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 6E n */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 6F o */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 70 p */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 71 q */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 72 r */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 73 s */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 74 t */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 75 u */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 76 v */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 77 w */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 78 x */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 79 y */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 7A z */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 7B { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 7C | */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 7D } */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 7E ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 7F */
};

/* ================================================================
 * Generic draw helpers (operate on a caller-supplied pixel buffer)
 * ================================================================ */
static void fill(unsigned int *p, int pw, int ph,
                 int x, int y, int w, int h, unsigned int c) {
    for (int r = y; r < y+h; r++) {
        if (r < 0 || r >= ph) continue;
        for (int col = x; col < x+w; col++) {
            if (col < 0 || col >= pw) continue;
            p[r*pw + col] = c;
        }
    }
}

static void glyph(unsigned int *p, int pw, int ph,
                  int x, int y, unsigned char ch, unsigned int c) {
    if (ch >= 128) ch = '?';
    const unsigned char *g = font8[ch];
    for (int r = 0; r < 8; r++) {
        if (y+r < 0 || y+r >= ph) continue;
        unsigned char bits = g[r];
        for (int col = 0; col < 8; col++) {
            int px0 = x + col;
            if (px0 < 0 || px0 >= pw) continue;
            if (bits & (1u << col))
                p[(y+r)*pw + px0] = c;
        }
    }
}

static void draw_str(unsigned int *p, int pw, int ph,
                     int x, int y, const char *s, unsigned int c) {
    for (int i = 0; s[i]; i++)
        glyph(p, pw, ph, x + i*8, y, (unsigned char)s[i], c);
}

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void draw_str_cx(unsigned int *p, int pw, int ph,
                        int rx, int rw, int y, const char *s, unsigned int c) {
    int tw = slen(s) * 8;
    draw_str(p, pw, ph, rx + (rw - tw)/2, y, s, c);
}

static void hline(unsigned int *p, int pw, int ph,
                  int x, int y, int w, unsigned int c) {
    for (int i = x; i < x+w; i++) {
        if (i >= 0 && i < pw && y >= 0 && y < ph)
            p[y*pw+i] = c;
    }
}

static void border(unsigned int *p, int pw, int ph,
                   int x, int y, int w, int h, unsigned int c) {
    for (int i = x+1; i < x+w-1; i++) {
        if (y >= 0 && y < ph)         p[y*pw+i]         = c;
        if (y+h-1 >= 0 && y+h-1 < ph) p[(y+h-1)*pw+i]  = c;
    }
    for (int i = y+1; i < y+h-1; i++) {
        if (i >= 0 && i < ph) {
            p[i*pw+x]     = c;
            p[i*pw+x+w-1] = c;
        }
    }
}

/* ================================================================
 * String helpers
 * ================================================================ */

/* Returns 1 if name ends with ".elf" (case-insensitive). */
static int ends_with_elf(const char *name) {
    int n = slen(name);
    if (n < 5) return 0;
    return (name[n-4] == '.') &&
           ((name[n-3] | 0x20) == 'e') &&
           ((name[n-2] | 0x20) == 'l') &&
           ((name[n-1] | 0x20) == 'f');
}

/* Copy at most dst_max-1 chars, lowercase, stop at '\0' or '.' */
static void copy_stem_lower(char *dst, const char *src, int dst_max) {
    int i;
    for (i = 0; src[i] && src[i] != '.' && i < dst_max - 1; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        dst[i] = c;
    }
    dst[i] = '\0';
}

/* Append ".elf" to dst in-place; dst must have room. */
static void append_elf(char *dst) {
    int n = slen(dst);
    dst[n]   = '.'; dst[n+1] = 'e';
    dst[n+2] = 'l'; dst[n+3] = 'f';
    dst[n+4] = '\0';
}

/* Integer to decimal string (positive only, no leading zeros) */
static void itoa_simple(int v, char *buf) {
    if (v == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[12]; int i=0;
    while (v > 0) { tmp[i++] = (char)('0' + v%10); v /= 10; }
    int j; for (j=0; j<i; j++) buf[j] = tmp[i-1-j];
    buf[j] = '\0';
}

/* ================================================================
 * File browser state
 * ================================================================ */
/* Stem names (without ".elf"), lowercase — max 12 chars + NUL */
static char elf_stems[MAX_ELF][13];
static int  elf_count  = 0;
static int  br_hover   = -1;   /* index of hovered entry (-1 = none) */
static int  br_scroll  = 0;    /* scroll offset in entries            */
static int  browser_wid = -1;  /* -1 = closed                        */

/* Scan root directory for .elf files and populate elf_stems[]. */
static void enumerate_elfs(void) {
    elf_count = 0;
    DIR *d = opendir("/");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != (struct dirent *)0) {
        if (elf_count >= MAX_ELF) break;
        if (!ends_with_elf(e->d_name)) continue;
        copy_stem_lower(elf_stems[elf_count], e->d_name, 13);
        elf_count++;
    }
    closedir(d);
}

/* ================================================================
 * Browser rendering
 * ================================================================ */
static void draw_browser(unsigned int *br_px) {
    /* ---- Header bar ---- */
    fill(br_px, BR_W, BR_H, 0, 0, BR_W, BR_HDR_H, C_BR_HDR);
    draw_str(br_px, BR_W, BR_H, 6, (BR_HDR_H - 8)/2, "Applications", C_TEXT_C);

    /* Count / total hint (right-aligned in header) */
    if (elf_count > 0) {
        char buf[24];
        char n1[8], n2[8];
        itoa_simple(elf_count, n1);
        itoa_simple(MAX_ELF, n2);   /* use actual count, not MAX */
        /* build "N apps" string */
        int i=0; const char *src = n1;
        while (*src) buf[i++] = *src++;
        buf[i++]=' '; buf[i++]='a'; buf[i++]='p'; buf[i++]='p';
        buf[i++]='s'; buf[i] = '\0';
        draw_str(br_px, BR_W, BR_H,
                 BR_W - slen(buf)*8 - 6, (BR_HDR_H - 8)/2,
                 buf, C_TEXT_DIM);
    }

    hline(br_px, BR_W, BR_H, 0, BR_HDR_H - 1, BR_W, C_BR_SEP);

    /* ---- Scrollable list ---- */
    fill(br_px, BR_W, BR_H, 0, BR_HDR_H, BR_W, BR_H - BR_HDR_H, C_BR_BG);

    for (int i = 0; i < BR_VISIBLE; i++) {
        int fi = i + br_scroll;
        if (fi >= elf_count) break;

        int ey  = BR_HDR_H + i * BR_ENTRY_H;
        int hov = (fi == br_hover);

        /* Row background */
        fill(br_px, BR_W, BR_H, 0, ey, BR_W, BR_ENTRY_H,
             hov ? C_BR_HOV : C_BR_ENTRY);

        /* "> " arrow icon for hover, dim bullet otherwise */
        if (hov) {
            glyph(br_px, BR_W, BR_H, 5, ey + (BR_ENTRY_H-8)/2, '>', C_BR_ICON);
        } else {
            /* small dash bullet */
            glyph(br_px, BR_W, BR_H, 6, ey + (BR_ENTRY_H-8)/2, '-', C_BR_SEP);
        }

        /* File stem name */
        unsigned int fg = hov ? C_BR_TXT_H : C_BR_TXT;
        draw_str(br_px, BR_W, BR_H, 18, ey + (BR_ENTRY_H-8)/2, elf_stems[fi], fg);

        /* Separator line at bottom of each entry */
        hline(br_px, BR_W, BR_H, 0, ey + BR_ENTRY_H - 1, BR_W, C_BR_SEP);
    }

    /* ---- Scroll indicator bar (right edge) ---- */
    if (elf_count > BR_VISIBLE) {
        /* Track */
        fill(br_px, BR_W, BR_H, BR_W - 4, BR_HDR_H, 4, BR_H - BR_HDR_H, C_BR_SEP);

        /* Thumb */
        int track_h = BR_H - BR_HDR_H;
        int thumb_h = (BR_VISIBLE * track_h) / elf_count;
        if (thumb_h < 8) thumb_h = 8;
        int thumb_y = BR_HDR_H + (br_scroll * (track_h - thumb_h))
                      / (elf_count - BR_VISIBLE);
        fill(br_px, BR_W, BR_H, BR_W - 4, thumb_y, 4, thumb_h, C_BR_SCROLL);
    }

    /* ---- Up/Down hint (only when scrollable) ---- */
    if (elf_count > BR_VISIBLE) {
        char hint[] = "^ v to scroll";
        draw_str_cx(br_px, BR_W, BR_H, 0, BR_W - 4,
                    BR_H - 9, hint, C_TEXT_DIM);
    }
}

/* ================================================================
 * Menu bar
 * ================================================================ */
#define MB_LOGO_X  60
static const char *mb_items[] = { "File", "View", "Apps", "Help" };
#define MB_N   4
#define MB_IW  48

static void draw_menubar(unsigned int *mb) {
    fill(mb, MB_W, MB_H, 0, 0, MB_W, MB_H, C_MB_BG);
    draw_str(mb, MB_W, MB_H, MB_LOGO_X, (MB_H-8)/2, "SiMPLE OS", C_TEXT_C);
    int mx = MB_LOGO_X + 9*8 + 20;
    for (int i = 0; i < MB_N; i++)
        draw_str(mb, MB_W, MB_H, mx + i*MB_IW, (MB_H-8)/2, mb_items[i], C_TEXT_DIM);
    const char *rv = "SiMPLE OS v1.0";
    draw_str(mb, MB_W, MB_H, MB_W - slen(rv)*8 - 8, (MB_H-8)/2, rv, C_TEXT_DIM);
    hline(mb, MB_W, MB_H, 0, MB_H-1, MB_W, C_SEP);
}

/* ================================================================
 * Dock
 * ================================================================ */
#define N_BTNS   5
#define BTN_W    130
#define BTN_H    42
#define BTN_GAP  12

#define BTN_STRIP  (N_BTNS*BTN_W + (N_BTNS-1)*BTN_GAP)
#define BTN_X0     ((DK_W - BTN_STRIP) / 2)
#define BTN_Y0     ((DK_H - BTN_H) / 2)   /* = 5 */

/* btn_app: empty string "" means the button has a custom handler */
static const char *btn_label[N_BTNS]  = { "TERMINAL", "CALC", "FILES", "PAINT", "SNAKE" };
static const char *btn_app[N_BTNS]    = { "term.elf", "calc.elf", "", "paint.elf", "snake.elf" };
static const unsigned int btn_acc[N_BTNS] = { 0x44FFCC, 0xFF8844, 0x8888FF, 0xFF6644, 0x44FF77 };

static int bx(int i) { return BTN_X0 + i*(BTN_W + BTN_GAP); }

static int btn_hit(int mx, int my, int i) {
    return (mx >= bx(i)) && (mx < bx(i)+BTN_W) &&
           (my >= BTN_Y0) && (my < BTN_Y0+BTN_H);
}

static void draw_dock(unsigned int *dk, int hov) {
    fill(dk, DK_W, DK_H, 0, 0, DK_W, DK_H, C_DK_BG);
    hline(dk, DK_W, DK_H, 0, 0, DK_W, C_SEP);

    for (int i = 0; i < N_BTNS; i++) {
        /* FILES button: show as active (different accent) when browser is open */
        int files_active = (i == 2 && browser_wid >= 0);
        unsigned int bg  = (i == hov || files_active) ? C_DK_HOV  : C_DK_BTN;
        unsigned int bdr = (i == hov || files_active) ? btn_acc[i] : C_DK_BDR;
        /* Dim if no app wired AND not FILES */
        unsigned int tc  = (i != 2 && !btn_app[i][0]) ? C_TEXT_DIM
                         : (i == hov || files_active) ? btn_acc[i] : C_TEXT_W;

        fill  (dk, DK_W, DK_H, bx(i), BTN_Y0, BTN_W, BTN_H, bg);
        border(dk, DK_W, DK_H, bx(i), BTN_Y0, BTN_W, BTN_H, bdr);

        if (i == hov || files_active)
            hline(dk, DK_W, DK_H, bx(i)+2, BTN_Y0, BTN_W-4, btn_acc[i]);

        draw_str_cx(dk, DK_W, DK_H, bx(i), BTN_W,
                    BTN_Y0 + (BTN_H-8)/2, btn_label[i], tc);
    }
}

/* ================================================================
 * Browser open / close helpers
 * ================================================================ */
static void open_browser(unsigned int *br_px) {
    if (browser_wid >= 0) return;
    enumerate_elfs();
    br_hover  = -1;
    br_scroll = 0;
    browser_wid = wm_create(BR_WX, BR_WY, BR_W, BR_H);
    if (browser_wid < 0) { browser_wid = -1; return; }
    wm_setfocus(browser_wid);
    draw_browser(br_px);
    wm_blit(browser_wid, br_px, BR_W * BR_H * 4);
}

static void close_browser(void) {
    if (browser_wid < 0) return;
    wm_destroy(browser_wid);
    browser_wid = -1;
}

/* ================================================================
 * Entry point
 * ================================================================ */
void _start(void) {
    /* Allocate pixel buffers from user-space heap */
    int mb_brk = sbrk(MB_W * MB_H * 4);
    if (mb_brk < 0) exit(1);
    unsigned int *mb_px = (unsigned int *)(unsigned int)mb_brk;

    int dk_brk = sbrk(DK_W * DK_H * 4);
    if (dk_brk < 0) exit(1);
    unsigned int *dk_px = (unsigned int *)(unsigned int)dk_brk;

    /* Pre-allocate the browser pixel buffer (reused across open/close) */
    int br_brk = sbrk(BR_W * BR_H * 4);
    if (br_brk < 0) exit(1);
    unsigned int *br_px = (unsigned int *)(unsigned int)br_brk;

    /* Create windows */
    int menu_wid = wm_create(MB_WX, MB_WY, MB_W, MB_H);
    if (menu_wid < 0) exit(1);

    int dock_wid = wm_create(DK_WX, DK_WY, DK_W, DK_H);
    if (dock_wid < 0) { wm_destroy(menu_wid); exit(1); }

    /* Initial draw */
    draw_menubar(mb_px);
    wm_blit(menu_wid, mb_px, MB_W * MB_H * 4);

    int hov = -1;
    draw_dock(dk_px, hov);
    wm_blit(dock_wid, dk_px, DK_W * DK_H * 4);

    wm_setfocus(dock_wid);

    wm_event_t ev;
    for (;;) {
        int r = wm_event(&ev, (int)sizeof(ev));
        if (r <= 0) { yield(); continue; }

        /* ESC from any window → exit desktop */
        if (r == WM_EV_KEY_DOWN && ((int)ev.x & 0xFF) == 0x01) break;

        /* ---- Browser window events ---- */
        if ((int)ev.wid == browser_wid && browser_wid >= 0) {

            if (r == WM_EV_CLOSE) {
                close_browser();
                /* Redraw dock (FILES button de-highlights) */
                draw_dock(dk_px, hov);
                wm_blit(dock_wid, dk_px, DK_W * DK_H * 4);
            }

            else if (r == WM_EV_MOUSE_MOV) {
                int mx = (int)ev.x, my = (int)ev.y;
                int ni = -1;
                if (my >= BR_HDR_H) {
                    int idx = (my - BR_HDR_H) / BR_ENTRY_H + br_scroll;
                    if (idx >= 0 && idx < elf_count) ni = idx;
                }
                if (ni != br_hover) {
                    br_hover = ni;
                    draw_browser(br_px);
                    wm_blit(browser_wid, br_px, BR_W * BR_H * 4);
                }
            }

            else if (r == WM_EV_MOUSE_BTN && (ev.btn & 1)) {
                int my = (int)ev.y;
                if (my >= BR_HDR_H) {
                    int idx = (my - BR_HDR_H) / BR_ENTRY_H + br_scroll;
                    if (idx >= 0 && idx < elf_count) {
                        /* Build "stem.elf" path */
                        char path[18];
                        int k = slen(elf_stems[idx]);
                        int i; for (i = 0; i < k && i < 12; i++)
                            path[i] = elf_stems[idx][i];
                        path[i] = '\0';
                        append_elf(path);
                        int pid = fork();
                        if (pid == 0) { exec(path); exit(1); }
                        /* Don't close browser — let user launch multiple apps */
                    }
                }
            }

            else if (r == WM_EV_KEY_DOWN) {
                int sc = (int)ev.x & 0xFF;
                int redraw = 0;
                if (sc == 0x48 && br_scroll > 0) {          /* ↑ */
                    br_scroll--;
                    redraw = 1;
                } else if (sc == 0x50 &&
                           br_scroll + BR_VISIBLE < elf_count) { /* ↓ */
                    br_scroll++;
                    redraw = 1;
                } else if (sc == 0x01) {                     /* ESC */
                    close_browser();
                    draw_dock(dk_px, hov);
                    wm_blit(dock_wid, dk_px, DK_W * DK_H * 4);
                }
                if (redraw && browser_wid >= 0) {
                    draw_browser(br_px);
                    wm_blit(browser_wid, br_px, BR_W * BR_H * 4);
                }
            }
        }

        /* ---- Dock events ---- */
        else if ((int)ev.wid == dock_wid) {

            if (r == WM_EV_CLOSE) break;

            if (r == WM_EV_MOUSE_MOV) {
                int nx = -1;
                int mx = (int)ev.x, my = (int)ev.y;
                for (int i = 0; i < N_BTNS; i++)
                    if (btn_hit(mx, my, i)) { nx = i; break; }
                if (nx != hov) {
                    hov = nx;
                    draw_dock(dk_px, hov);
                    wm_blit(dock_wid, dk_px, DK_W * DK_H * 4);
                }
            }

            if (r == WM_EV_MOUSE_BTN && (ev.btn & 1)) {
                int mx = (int)ev.x, my = (int)ev.y;
                for (int i = 0; i < N_BTNS; i++) {
                    if (!btn_hit(mx, my, i)) continue;

                    if (i == 2) {
                        /* FILES — toggle file browser */
                        if (browser_wid < 0)
                            open_browser(br_px);
                        else
                            close_browser();
                        draw_dock(dk_px, hov);
                        wm_blit(dock_wid, dk_px, DK_W * DK_H * 4);
                    } else if (btn_app[i][0]) {
                        /* Regular app — fork + exec */
                        int pid = fork();
                        if (pid == 0) { exec(btn_app[i]); exit(1); }
                    }
                    break;
                }
            }
        }

        /* ---- Menu bar events — static, ignore ---- */
    }

    close_browser();
    wm_destroy(dock_wid);
    wm_destroy(menu_wid);
    exit(0);
}
