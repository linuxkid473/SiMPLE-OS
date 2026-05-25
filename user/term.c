/*
 * term.c v2 — True terminal emulator for SiMPLE OS
 *
 * Ports kernel/src/shell.c command logic into a WM window.
 * 80 cols × 20 rows at 8×16 per character (8×8 font doubled vertically).
 * All output goes to a scrollback ring buffer rendered into the pixel framebuffer.
 *
 * Available syscalls via libc.c / syscall.h:
 *   open/close/fd_read/fd_write/seek  — file I/O (root-level only in kernel)
 *   stat/readdir                       — filesystem metadata
 *   exec/fork/wait                     — process management
 *   getticks/sleep/getpid              — misc
 *   wm_create/destroy/blit/event/setfocus — windowing
 *
 * Limitation: SYS_OPEN / SYS_STAT / SYS_READDIR only accept root-relative
 * 8.3 names (the kernel passes dir_cluster=0 for all user opens).
 * cd/ls work one level deep via READDIR; file ops work at root only.
 * No mkdir or rm from user space (no kernel syscall for them).
 */

#include "wm.h"
#include "syscall.h"   /* stat_t, dirent_t, readdir(), stat() */

/* ---- syscall wrappers from libc.c ---- */
void         exit(int code);
int          write(const char *buf, int len);
int          wm_create(int x, int y, int w, int h);
int          wm_destroy(int wid);
int          wm_blit(int wid, unsigned int *buf, int len);
int          wm_event(wm_event_t *ev, int max);
int          wm_setfocus(int wid);
int          exec(const char *path);
int          fork(void);
int          wait(void);
int          open(const char *path, int flags);
int          close(int fd);
int          fd_read(int fd, void *buf, int len);
int          fd_write(int fd, const void *buf, int len);
int          seek(int fd, int offset, int whence);
unsigned int getticks(void);
int          sys_sleep(unsigned int ticks);

/* ---- open flags (must match kernel/include/fd.h) ---- */
#define O_READ   1
#define O_WRITE  2
#define O_CREATE 4

/* ================================================================
 * Layout
 * ================================================================ */
#define WIN_X    80
#define WIN_Y    40
#define WIN_W    640
#define WIN_H    320
#define CHAR_W   8
#define CHAR_H   16    /* 8-row glyph drawn 2× tall */
#define COLS     (WIN_W / CHAR_W)  /* 80 */
#define ROWS     (WIN_H / CHAR_H)  /* 20 */
#define OUT_ROWS (ROWS - 1)        /* rows 0–18 for output */

/* ================================================================
 * Colors
 * ================================================================ */
#define C_BG      0x0C0C0C
#define C_TEXT    0x00DD66   /* green */
#define C_PROMPT  0x00CCFF   /* cyan  */
#define C_DIR     0x55AAFF   /* blue  for directory entries */
#define C_ERROR   0xFF5555   /* red   */
#define C_INFO    0xBBBBBB   /* grey  */
#define C_WARN    0xFFAA00   /* amber */
#define C_SEP     0x224422   /* separator line */
#define C_CURSOR  0x00FF88   /* cursor block */

/* ================================================================
 * 8×8 IBM VGA bitmap font (public domain) for ASCII 0x00–0x7F
 * Each glyph is 8 bytes; LSB of each byte = leftmost pixel column.
 * ================================================================ */
static const unsigned char font[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x00 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x01 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x02 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x03 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x04 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x05 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x06 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x07 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x08 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x09 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x0A */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x0B */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x0C */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x0D */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x0E */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x0F */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x10 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x11 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x12 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x13 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x14 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x15 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x16 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x17 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x18 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x19 */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x1A */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x1B */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x1C */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x1D */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x1E */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x1F */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 0x21 ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x22 " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 0x23 # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 0x24 $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 0x25 % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 0x26 & */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 0x27 ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 0x28 ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 0x29 ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 0x2A * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 0x2B + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 0x2C , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 0x2D - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 0x2E . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 0x2F / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 0x30 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 0x31 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 0x32 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 0x33 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 0x34 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 0x35 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 0x36 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 0x37 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 0x38 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 0x39 9 */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 0x3A : */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 0x3B ; */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 0x3C < */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 0x3D = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 0x3E > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 0x3F ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 0x40 @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 0x41 A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 0x42 B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 0x43 C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 0x44 D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 0x45 E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 0x46 F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 0x47 G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 0x48 H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x49 I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 0x4A J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 0x4B K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 0x4C L */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 0x4D M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 0x4E N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 0x4F O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 0x50 P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 0x51 Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 0x52 R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 0x53 S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x54 T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 0x55 U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 0x56 V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 0x57 W */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 0x58 X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 0x59 Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 0x5A Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 0x5B [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 0x5C \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 0x5D ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 0x5E ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 0x5F _ */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 0x60 ` */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 0x61 a */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 0x62 b */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 0x63 c */
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, /* 0x64 d */
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, /* 0x65 e */
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, /* 0x66 f */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 0x67 g */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 0x68 h */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x69 i */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 0x6A j */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 0x6B k */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x6C l */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 0x6D m */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 0x6E n */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 0x6F o */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 0x70 p */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 0x71 q */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 0x72 r */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 0x73 s */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 0x74 t */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 0x75 u */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 0x76 v */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 0x77 w */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 0x78 x */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 0x79 y */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 0x7A z */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 0x7B { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 0x7C | */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 0x7D } */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x7E ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x7F */
};

/* ================================================================
 * String utilities (no libc)
 * ================================================================ */

static int t_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static int t_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int t_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static void t_strcpy(char *d, const char *s) { while ((*d++=*s++)); }

static void t_strncpy(char *d, const char *s, int n) {
    int i;
    for (i = 0; i < n-1 && s[i]; i++) d[i] = s[i];
    d[i] = '\0';
}

static void t_strcat(char *d, const char *s) {
    d += t_strlen(d);
    while ((*d++=*s++));
}

/* Convert unsigned 32-bit integer to decimal string. */
static void u32_to_str(unsigned int v, char *out) {
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    char tmp[11]; int i = 0;
    while (v > 0) { tmp[i++] = (char)('0' + v%10); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

/* Convert unsigned 32-bit integer to hex string (no prefix). */
static void u32_to_hex(unsigned int v, char *out) {
    static const char hx[] = "0123456789ABCDEF";
    char tmp[8]; int i = 0;
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    while (v > 0) { tmp[i++] = hx[v & 0xF]; v >>= 4; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

/* Parse decimal string to int; returns -1 if invalid. */
static int str_to_int(const char *s) {
    if (!s || !*s) return -1;
    int neg = 0, v = 0;
    if (*s == '-') { neg = 1; s++; }
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v*10 + (*s - '0');
    }
    return neg ? -v : v;
}

static int is_space(char c) { return c == ' ' || c == '\t'; }

/* Skip leading whitespace, return pointer into s. */
static const char *skip_ws(const char *s) {
    while (is_space(*s)) s++;
    return s;
}

/* Tokenise: advance *p past whitespace to start, return token start,
 * NUL-terminate it, leave *p pointing after the token.
 * Returns NULL if nothing left. */
static char *next_tok(char **p) {
    if (!p || !*p) return 0;
    char *s = *p;
    while (is_space(*s)) s++;
    if (!*s) { *p = s; return 0; }
    char *start = s;
    while (*s && !is_space(*s)) s++;
    if (*s) { *s = '\0'; s++; }
    *p = s;
    return start;
}

/* ================================================================
 * Scrollback  (SCROLLBACK_MAX lines, COLS+1 bytes each)
 * ================================================================ */
#define SCROLLBACK_MAX 100
static char         sb_text[SCROLLBACK_MAX][COLS + 1];
static unsigned int sb_color[SCROLLBACK_MAX];
static int          sb_count = 0;

static void sb_push(const char *line, unsigned int color) {
    int idx = sb_count % SCROLLBACK_MAX;
    int i;
    for (i = 0; i < COLS && line[i]; i++) sb_text[idx][i] = line[i];
    sb_text[idx][i] = '\0';
    sb_color[idx] = color;
    sb_count++;
}

/* Print text splitting on '\n'; each fragment becomes one scrollback line. */
static void term_print(const char *s, unsigned int color) {
    char line[COLS + 1];
    int col = 0;
    for (int i = 0; ; i++) {
        char c = s[i];
        if (c == '\n' || c == '\0') {
            line[col] = '\0';
            sb_push(line, color);
            col = 0;
            if (!c) break;
        } else if (col < COLS) {
            line[col++] = c;
        }
    }
}

/* term_puts: print string + newline. */
static void tp(const char *s, unsigned int col) { term_print(s, col); }

/* ================================================================
 * Input / history
 * ================================================================ */
#define INPUT_MAX  256
#define HIST_MAX   10

static char input_buf[INPUT_MAX];
static int  input_len = 0;
static char history[HIST_MAX][INPUT_MAX];
static int  hist_count = 0;
static int  hist_idx   = -1;
static char hist_saved[INPUT_MAX];

/* Push command to history (skip empty and exact duplicates of head). */
static void hist_push(const char *cmd) {
    if (!cmd[0]) return;
    if (hist_count > 0 && t_strcmp(history[0], cmd) == 0) return;
    int limit = hist_count < HIST_MAX-1 ? hist_count : HIST_MAX-1;
    for (int i = limit; i > 0; i--) t_strcpy(history[i], history[i-1]);
    t_strncpy(history[0], cmd, INPUT_MAX);
    if (hist_count < HIST_MAX) hist_count++;
}

/* ================================================================
 * CWD tracking
 * ================================================================ */
#define CWD_MAX 256
static char cwd[CWD_MAX]; /* e.g. "/" or "/subdir" */

/* Build prompt string into buf, e.g. "SiMPLE ~/subdir > " */
static void build_prompt(char *buf, int cap) {
    int n = 0;
    const char *parts[] = {"SiMPLE ~", cwd[1] ? cwd : "", " > "};
    for (int i = 0; i < 3; i++) {
        for (const char *p = parts[i]; *p && n+1 < cap; p++) buf[n++] = *p;
    }
    buf[n] = '\0';
}

/* ================================================================
 * Keyboard / scancode
 * ================================================================ */
static int shift = 0;

static const char key_map[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0
};
static const char shift_map[128] = {
    0, 27,'!','@','#','$','%','^','&','*','(',')',  '_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' ',0
};

/* ================================================================
 * Cursor blink
 * ================================================================ */
static int          cursor_vis = 1;
static unsigned int last_blink = 0;
#define BLINK_TICKS 40

/* ================================================================
 * Rendering
 * ================================================================ */

/* Draw one 8×8 glyph at character cell (cx,cy), doubled vertically to 8×16. */
static void draw_char(int cx, int cy, unsigned char ch,
                      unsigned int color, unsigned int *px)
{
    if (ch >= 128) ch = '?';
    const unsigned char *g = font[ch];
    int px0 = cx * CHAR_W;
    int py0 = cy * CHAR_H;
    for (int row = 0; row < 8; row++) {
        unsigned char bits = g[row];
        int py = py0 + row*2;
        if (py+1 >= WIN_H) break;
        for (int col = 0; col < 8; col++) {
            int x = px0 + col;
            if (x >= WIN_W) break;
            unsigned int c = (bits & (1u << col)) ? color : C_BG;
            px[py      *WIN_W + x] = c;
            px[(py+1)  *WIN_W + x] = c;
        }
    }
}

/* Draw a filled 8×16 cursor block at cell (cx,cy). */
static void draw_cursor(int cx, int cy, unsigned int *px) {
    int px0 = cx*CHAR_W, py0 = cy*CHAR_H;
    for (int r = 0; r < CHAR_H; r++) {
        int py = py0 + r;
        if (py >= WIN_H) break;
        for (int c = 0; c < CHAR_W; c++) {
            int x = px0+c;
            if (x >= WIN_W) break;
            px[py*WIN_W + x] = C_CURSOR;
        }
    }
}

/* Full redraw: scrollback + separator + input line + cursor → blit. */
static void redraw(int wid, unsigned int *px) {
    /* Background */
    for (int i = 0; i < WIN_W*WIN_H; i++) px[i] = C_BG;

    /* Separator */
    int sep_y = OUT_ROWS*CHAR_H - 1;
    for (int x = 0; x < WIN_W; x++) px[sep_y*WIN_W + x] = C_SEP;

    /* Scrollback: last OUT_ROWS lines */
    int start = sb_count - OUT_ROWS;
    if (start < 0) start = 0;
    for (int r = 0; r < OUT_ROWS; r++) {
        int si = start + r;
        if (si >= sb_count) break;
        int idx = si % SCROLLBACK_MAX;
        unsigned int col = sb_color[idx];
        for (int c = 0; sb_text[idx][c] && c < COLS; c++)
            draw_char(c, r, (unsigned char)sb_text[idx][c], col, px);
    }

    /* Build prompt */
    char prompt[64];
    build_prompt(prompt, sizeof(prompt));
    int plen = t_strlen(prompt);

    /* Input row */
    int ir = ROWS - 1;
    for (int c = 0; c < plen && c < COLS; c++)
        draw_char(c, ir, (unsigned char)prompt[c], C_PROMPT, px);
    for (int c = 0; input_buf[c] && (plen+c) < COLS; c++)
        draw_char(plen+c, ir, (unsigned char)input_buf[c], C_TEXT, px);

    /* Cursor */
    if (cursor_vis) {
        int cx = plen + input_len;
        if (cx < COLS) draw_cursor(cx, ir, px);
    }

    wm_blit(wid, px, WIN_W*WIN_H*4);
}

/* ================================================================
 * Command helpers
 * ================================================================ */

/* Build an absolute path from CWD + name for readdir/stat.
 * Result in out (cap bytes). */
static void make_abs(const char *name, char *out, int cap) {
    if (name[0] == '/') {
        t_strncpy(out, name, cap);
        return;
    }
    /* CWD is always "/" or "/something" */
    if (cwd[1] == '\0') {
        /* root */
        out[0] = '/'; out[1] = '\0';
        int i = 1;
        for (int j = 0; name[j] && i+1 < cap; j++) out[i++] = name[j];
        out[i] = '\0';
    } else {
        t_strncpy(out, cwd, cap);
        /* append "/" + name */
        int n = t_strlen(out);
        if (n+1 < cap) out[n++] = '/';
        for (int j = 0; name[j] && n+1 < cap; j++) out[n++] = name[j];
        out[n] = '\0';
    }
}

/* ================================================================
 * Commands
 * ================================================================ */

static void cmd_help(void) {
    tp("Commands:", C_PROMPT);
    tp("  help              list commands", C_INFO);
    tp("  ls [dir]          list directory", C_INFO);
    tp("  cd <dir>          change directory", C_INFO);
    tp("  pwd               print working directory", C_INFO);
    tp("  cat <file>        print file contents", C_INFO);
    tp("  stat <file>       show file/dir metadata", C_INFO);
    tp("  touch <file>      create empty file", C_INFO);
    tp("  write <f> <text>  write text to file", C_INFO);
    tp("  run <file.elf>    run ELF (terminal stays)", C_INFO);
    tp("  <prog.elf>        exec directly (replaces term)", C_INFO);
    tp("  echo <text>       print text", C_INFO);
    tp("  clear             clear screen", C_INFO);
    tp("  getpid            print process ID", C_INFO);
    tp("  getticks          print PIT tick counter", C_INFO);
    tp("  sleep <n>         sleep n PIT ticks (100Hz)", C_INFO);
    tp("  exit              close terminal", C_INFO);
    tp("Note: mkdir/rm not available (no user syscall).", C_WARN);
    tp("Note: open/stat are root-level only.", C_WARN);
}

static void cmd_ls(const char *arg) {
    char path[CWD_MAX];
    if (arg && *arg) {
        make_abs(arg, path, sizeof(path));
    } else {
        t_strncpy(path, cwd, sizeof(path));
    }

    #define LS_MAX 64
    static dirent_t ents[LS_MAX];
    int n = readdir(path, ents, LS_MAX);
    if (n < 0) {
        tp("ls: cannot read directory", C_ERROR);
        return;
    }
    if (n == 0) {
        tp("(empty)", C_INFO);
        return;
    }
    char line[COLS+1];
    for (int i = 0; i < n; i++) {
        char szstr[12];
        u32_to_str(ents[i].size, szstr);
        line[0] = '\0';
        if (ents[i].is_dir) {
            t_strcat(line, "[DIR]  ");
            t_strcat(line, ents[i].name);
            sb_push(line, C_DIR);
        } else {
            /* "       name.ext  (12345 bytes)" */
            t_strcat(line, "       ");
            t_strcat(line, ents[i].name);
            t_strcat(line, "  (");
            t_strcat(line, szstr);
            t_strcat(line, " bytes)");
            sb_push(line, C_TEXT);
        }
    }
}

static void cmd_cd(const char *arg) {
    if (!arg || !*arg) { tp("cd: missing argument", C_ERROR); return; }

    /* Handle ".." */
    if (t_strcmp(arg, "..") == 0) {
        if (cwd[1] == '\0') return; /* already root */
        /* Pop last component */
        int last = 0;
        for (int i = 1; cwd[i]; i++) if (cwd[i] == '/') last = i;
        if (last == 0) { cwd[1] = '\0'; } /* back to root */
        else           { cwd[last] = '\0'; }
        return;
    }

    /* Handle "~" or "/" */
    if (t_strcmp(arg, "~") == 0 || t_strcmp(arg, "/") == 0) {
        cwd[0] = '/'; cwd[1] = '\0';
        return;
    }

    /* Build candidate path and verify it's a directory */
    char candidate[CWD_MAX];
    make_abs(arg, candidate, sizeof(candidate));

    stat_t st;
    if (stat(candidate, &st) < 0 || !st.exists) {
        tp("cd: not found", C_ERROR);
        return;
    }
    if (!st.is_dir) {
        tp("cd: not a directory", C_ERROR);
        return;
    }

    t_strncpy(cwd, candidate, CWD_MAX);
}

static void cmd_pwd(void) {
    tp(cwd, C_TEXT);
}

/* Print file contents via SYS_OPEN + SYS_FREAD.
 * Note: SYS_OPEN only works at root — subdirectory paths will fail. */
static void cmd_cat(const char *name) {
    if (!name || !*name) { tp("cat: missing filename", C_ERROR); return; }

    /* Use the raw name; SYS_OPEN strips leading '/' and always looks at root */
    int fd = open(name, O_READ);
    if (fd < 0) {
        char msg[COLS+1];
        msg[0] = '\0';
        t_strcat(msg, "cat: cannot open: ");
        t_strcat(msg, name);
        tp(msg, C_ERROR);
        return;
    }

    static char cat_buf[4096];
    static char line_buf[COLS+1];
    int col = 0;

    while (1) {
        int n = fd_read(fd, cat_buf, sizeof(cat_buf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            char c = cat_buf[i];
            if (c == '\n' || c == '\r') {
                line_buf[col] = '\0';
                sb_push(line_buf, C_TEXT);
                col = 0;
            } else if (c >= 32 && col < COLS) {
                line_buf[col++] = c;
            }
        }
    }
    if (col > 0) {
        line_buf[col] = '\0';
        sb_push(line_buf, C_TEXT);
    }

    close(fd);
}

static void cmd_stat(const char *name) {
    if (!name || !*name) { tp("stat: missing filename", C_ERROR); return; }

    char path[CWD_MAX];
    make_abs(name, path, sizeof(path));

    stat_t st;
    if (stat(path, &st) < 0) {
        tp("stat: error", C_ERROR);
        return;
    }
    if (!st.exists) {
        char msg[COLS+1]; msg[0]='\0';
        t_strcat(msg, "stat: not found: "); t_strcat(msg, name);
        tp(msg, C_ERROR); return;
    }

    char line[COLS+1];
    line[0]='\0'; t_strcat(line,"name: "); t_strcat(line, name); tp(line,C_TEXT);
    line[0]='\0'; t_strcat(line,"type: "); t_strcat(line, st.is_dir ? "directory":"file"); tp(line,C_TEXT);
    if (!st.is_dir) {
        char szstr[12]; u32_to_str(st.size, szstr);
        line[0]='\0'; t_strcat(line,"size: "); t_strcat(line, szstr); t_strcat(line," bytes"); tp(line,C_TEXT);
    }
}

static void cmd_touch(const char *name) {
    if (!name || !*name) { tp("touch: missing filename", C_ERROR); return; }
    int fd = open(name, O_WRITE | O_CREATE);
    if (fd < 0) {
        tp("touch: failed (root-level only, 8.3 name required)", C_ERROR);
        return;
    }
    close(fd);
}

static void cmd_write(const char *name, const char *text) {
    if (!name || !*name) { tp("write: missing filename", C_ERROR); return; }
    if (!text) text = "";

    int fd = open(name, O_WRITE | O_CREATE);
    if (fd < 0) {
        tp("write: cannot open file (root-level only)", C_ERROR);
        return;
    }
    int len = t_strlen(text);
    if (len > 0) fd_write(fd, text, len);
    close(fd);

    char msg[COLS+1]; char ns[12];
    u32_to_str((unsigned int)len, ns);
    msg[0]='\0'; t_strcat(msg,"wrote "); t_strcat(msg,ns); t_strcat(msg," bytes");
    tp(msg, C_TEXT);
}

/* run: fork → child exec's the ELF → parent waits, then resets focus.
 * Tries name as-is first, then with ".elf" appended. */
static void cmd_run(int wid, const char *name, unsigned int *px) {
    if (!name || !*name) { tp("run: missing filename", C_ERROR); return; }

    char elf_name[32];
    t_strncpy(elf_name, name, sizeof(elf_name)-4);
    /* Check if already ends in .elf */
    int n = t_strlen(elf_name);
    int has_ext = (n > 4 && t_strcmp(elf_name+n-4, ".elf") == 0);

    char msg[COLS+1];
    msg[0]='\0'; t_strcat(msg,"Running: "); t_strcat(msg, name);
    tp(msg, C_INFO);
    redraw(wid, px);

    int pid = fork();
    if (pid == 0) {
        /* Child: exec the program */
        exec(elf_name);
        if (!has_ext) {
            char with_elf[36];
            t_strncpy(with_elf, elf_name, sizeof(with_elf)-4);
            t_strcat(with_elf, ".elf");
            exec(with_elf);
        }
        exit(1);
    }
    if (pid < 0) {
        tp("run: fork failed", C_ERROR);
        return;
    }
    /* Parent: wait for child */
    wait();

    /* Reclaim focus and redraw */
    wm_setfocus(wid);
    tp("Done.", C_INFO);
}

static void cmd_echo(const char *text) {
    if (!text || !*text) { sb_push("", C_TEXT); return; }
    tp(text, C_TEXT);
}

static void cmd_sleep(const char *arg) {
    if (!arg) { tp("sleep: missing argument", C_ERROR); return; }
    int t = str_to_int(arg);
    if (t < 0) { tp("sleep: invalid tick count", C_ERROR); return; }
    sys_sleep((unsigned int)t);
    char msg[32]; char ns[12]; u32_to_str((unsigned int)t, ns);
    msg[0]='\0'; t_strcat(msg,"slept "); t_strcat(msg,ns); t_strcat(msg," ticks");
    tp(msg, C_TEXT);
}

static void cmd_getpid(void) {
    char msg[32]; char ns[12];
    /* SYS_GETPID is declared in syscall.h as inline getpid() */
    int pid = 0;
    __asm__ volatile("int $0x80" : "=a"(pid) : "a"(13));
    u32_to_str((unsigned int)pid, ns);
    msg[0]='\0'; t_strcat(msg,"pid: "); t_strcat(msg,ns);
    tp(msg, C_TEXT);
}

static void cmd_getticks(void) {
    char msg[32]; char ns[12];
    u32_to_str(getticks(), ns);
    msg[0]='\0'; t_strcat(msg,"ticks: "); t_strcat(msg,ns);
    tp(msg, C_TEXT);
}

/* Try to exec command directly if it looks like an ELF name or unknown command. */
static void try_exec_unknown(int wid, const char *cmd, unsigned int *px) {
    /* Direct exec replaces this terminal — used for commands without "run" prefix */
    char msg[COLS+1];
    msg[0]='\0'; t_strcat(msg,"Exec: "); t_strcat(msg, cmd);
    tp(msg, C_INFO);
    redraw(wid, px);

    wm_destroy(wid);

    exec(cmd);

    /* Try with .elf */
    char with_elf[36];
    t_strncpy(with_elf, cmd, sizeof(with_elf)-4);
    t_strcat(with_elf, ".elf");
    exec(with_elf);

    /* If we get here, exec failed — we can no longer recover the window */
    /* Kernel shell will be waiting for us since exec failed */
    exit(1);
}

/* ================================================================
 * Command dispatcher
 * ================================================================ */

/* Echo the submitted command line to scrollback as a prompt-prefixed line. */
static void echo_cmd(const char *cmd) {
    char prompt[64];
    build_prompt(prompt, sizeof(prompt));
    char line[COLS+1];
    line[0]='\0';
    t_strcat(line, prompt);
    int plen = t_strlen(prompt);
    for (int i = 0; cmd[i] && plen+i < COLS; i++) line[plen+i] = cmd[i];
    line[plen + t_strlen(cmd)] = '\0';
    if (plen + t_strlen(cmd) > COLS) line[COLS] = '\0';
    sb_push(line, C_PROMPT);
}

static void dispatch(int wid, char *line_in, unsigned int *px) {
    /* Trim leading/trailing whitespace */
    const char *s = skip_ws(line_in);
    if (!*s) return;

    /* Copy to mutable buffer for tokenisation */
    static char cmd_buf[INPUT_MAX];
    t_strncpy(cmd_buf, s, INPUT_MAX);

    /* Trim trailing whitespace */
    int len = t_strlen(cmd_buf);
    while (len > 0 && is_space(cmd_buf[len-1])) cmd_buf[--len] = '\0';
    if (!len) return;

    echo_cmd(cmd_buf);
    hist_push(cmd_buf);
    hist_idx = -1;

    char *p = cmd_buf;
    char *cmd = next_tok(&p);
    if (!cmd) return;

    if (t_strcmp(cmd,"help")     == 0) { cmd_help(); }
    else if (t_strcmp(cmd,"clear") == 0) { sb_count = 0; }
    else if (t_strcmp(cmd,"pwd")   == 0) { cmd_pwd(); }
    else if (t_strcmp(cmd,"exit")  == 0) { wm_destroy(wid); exit(0); }

    else if (t_strcmp(cmd,"echo") == 0) {
        const char *rest = skip_ws(p);
        cmd_echo(*rest ? rest : "");
    }

    else if (t_strcmp(cmd,"ls") == 0) {
        char *arg = next_tok(&p);
        if (next_tok(&p)) { tp("usage: ls [dir]", C_ERROR); }
        else cmd_ls(arg);
    }

    else if (t_strcmp(cmd,"cd") == 0) {
        char *arg = next_tok(&p);
        if (!arg || next_tok(&p)) { tp("usage: cd <dir>", C_ERROR); }
        else cmd_cd(arg);
    }

    else if (t_strcmp(cmd,"cat")  == 0 ||
             t_strcmp(cmd,"open") == 0) {
        char *arg = next_tok(&p);
        if (!arg || next_tok(&p)) { tp("usage: cat <file>", C_ERROR); }
        else cmd_cat(arg);
    }

    else if (t_strcmp(cmd,"stat") == 0) {
        char *arg = next_tok(&p);
        if (!arg || next_tok(&p)) { tp("usage: stat <file>", C_ERROR); }
        else cmd_stat(arg);
    }

    else if (t_strcmp(cmd,"touch") == 0) {
        char *arg = next_tok(&p);
        if (!arg || next_tok(&p)) { tp("usage: touch <file>", C_ERROR); }
        else cmd_touch(arg);
    }

    else if (t_strcmp(cmd,"write") == 0) {
        char *file = next_tok(&p);
        if (!file) { tp("usage: write <file> <text>", C_ERROR); }
        else {
            const char *text = skip_ws(p);
            cmd_write(file, *text ? text : "");
        }
    }

    else if (t_strcmp(cmd,"run") == 0) {
        char *arg = next_tok(&p);
        if (!arg || next_tok(&p)) { tp("usage: run <file.elf>", C_ERROR); }
        else cmd_run(wid, arg, px);
    }

    else if (t_strcmp(cmd,"getpid")   == 0) cmd_getpid();
    else if (t_strcmp(cmd,"getticks") == 0) cmd_getticks();

    else if (t_strcmp(cmd,"sleep") == 0) {
        char *arg = next_tok(&p);
        cmd_sleep(arg);
    }

    else if (t_strcmp(cmd,"mkdir") == 0 || t_strcmp(cmd,"rm") == 0 ||
             t_strcmp(cmd,"cp")    == 0 || t_strcmp(cmd,"mv") == 0) {
        tp("Not available: no kernel syscall for this operation.", C_WARN);
    }

    else {
        /* Unknown command: try to exec it as a program */
        try_exec_unknown(wid, cmd, px);
        /* try_exec_unknown only returns if exec failed — we've lost the window
         * so the process exits */
    }
}

/* ================================================================
 * History navigation
 * ================================================================ */
static void hist_prev(void) {
    if (!hist_count) return;
    if (hist_idx == -1) { t_strncpy(hist_saved, input_buf, INPUT_MAX); hist_idx = 0; }
    else if (hist_idx < hist_count-1) hist_idx++;
    else return;
    t_strncpy(input_buf, history[hist_idx], INPUT_MAX);
    input_len = t_strlen(input_buf);
}

static void hist_next(void) {
    if (hist_idx == -1) return;
    if (hist_idx > 0) { hist_idx--; t_strncpy(input_buf, history[hist_idx], INPUT_MAX); }
    else { hist_idx = -1; t_strncpy(input_buf, hist_saved, INPUT_MAX); }
    input_len = t_strlen(input_buf);
}

/* ================================================================
 * IMPORTANT: declare pixel buffer LAST so it lands at the end of BSS.
 * The shell reads only 64KB of the ELF file; everything above must fit
 * in that window. With ~40KB of code+rodata and ~25KB of other BSS,
 * the total before pixels is ≈65KB — the pixel buffer starts just past
 * the 64KB mark and is zero-filled by the ELF loader from kernel memory.
 * Since redraw() clears pixels[] completely every frame this is safe.
 * ================================================================ */
static unsigned int pixels[WIN_W * WIN_H];

/* ================================================================
 * Entry point
 * ================================================================ */
void _start(void) {
    int wid = wm_create(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) {
        write("term: wm_create failed\n", 23);
        exit(1);
    }
    wm_setfocus(wid);

    /* Initialise state */
    cwd[0] = '/'; cwd[1] = '\0';
    input_buf[0] = '\0';

    tp("SiMPLE Terminal v2.0 — type 'help' for commands", C_PROMPT);
    tp("(File ops: root-level only. mkdir/rm unavailable.)", C_WARN);

    last_blink = getticks();
    redraw(wid, pixels);

    wm_event_t ev;
    while (1) {
        /* Cursor blink */
        unsigned int t = getticks();
        if (t - last_blink >= BLINK_TICKS) {
            cursor_vis ^= 1;
            last_blink  = t;
            redraw(wid, pixels);
        }

        int r = wm_event(&ev, (int)sizeof(ev));
        if (r == WM_EV_CLOSE) break;
        if (r != WM_EV_KEY_DOWN && r != WM_EV_KEY_UP) continue;

        int sc = (int)(ev.x) & 0xFF;

        /* Shift tracking */
        if (r == WM_EV_KEY_UP) {
            int base = sc & 0x7F;
            if (base == 0x2A || base == 0x36) shift = 0;
            continue;
        }

        /* KEY_DOWN */
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }
        if (sc == 0x01) break;           /* ESC → exit */

        if (sc == 0x0E) {                /* Backspace */
            if (input_len > 0) { input_len--; input_buf[input_len] = '\0'; }
            cursor_vis = 1; last_blink = getticks();
            redraw(wid, pixels);
            continue;
        }

        if (sc == 0x1C) {                /* Enter */
            dispatch(wid, input_buf, pixels);
            input_buf[0] = '\0'; input_len = 0;
            cursor_vis = 1; last_blink = getticks();
            redraw(wid, pixels);
            continue;
        }

        if (sc == 0x48) { hist_prev(); redraw(wid, pixels); continue; } /* Up */
        if (sc == 0x50) { hist_next(); redraw(wid, pixels); continue; } /* Down */
        if (sc == 0x4B || sc == 0x4D)   continue;                        /* L/R ignored */

        /* Printable character */
        if (sc < 64) {
            char c = shift ? shift_map[sc] : key_map[sc];
            if (c && c != '\b' && c != '\t' && c != '\n' && input_len < INPUT_MAX-1) {
                input_buf[input_len++] = c;
                input_buf[input_len]   = '\0';
                cursor_vis = 1; last_blink = getticks();
                redraw(wid, pixels);
            }
        }
    }

    wm_destroy(wid);
    exit(0);
}
