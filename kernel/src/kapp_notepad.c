#include "kapp.h"
#include "fat16.h"
#include "string.h"

/* ================================================================
 * Notepad — simple line editor, open/save FAT16 files
 * ================================================================ */

#define NP_COLS 48
#define NP_ROWS 60
#define NP_VIS  18

static char  buf[NP_ROWS][NP_COLS + 1];
static int   nlines, cx, cy, scroll;
static char  fname[32];
static char  status[48];
static int   fname_mode;   /* 0=edit text, 1=entering filename */
static int   fmode_save;   /* 1=save after filename entry, 0=open */
static char  fname_tmp[32];
static int   fname_tmp_len;

static void np_reset(void) {
    for (int i = 0; i < NP_ROWS; i++) buf[i][0] = '\0';
    nlines = 1; cx = 0; cy = 0; scroll = 0;
}

static void np_scroll_fix(void) {
    if (cy < scroll)           scroll = cy;
    if (cy >= scroll + NP_VIS) scroll = cy - NP_VIS + 1;
}

static void np_row_copy(int dst, int src) {
    int j = 0;
    while ((buf[dst][j] = buf[src][j])) j++;
}

static void np_insert(char c) {
    int len = (int)strlen(buf[cy]);
    if (len >= NP_COLS) return;
    for (int i = len; i >= cx; i--) buf[cy][i+1] = buf[cy][i];
    buf[cy][cx++] = c;
}

static void np_bs(void) {
    if (cx > 0) {
        int len = (int)strlen(buf[cy]);
        for (int i = cx-1; i < len; i++) buf[cy][i] = buf[cy][i+1];
        cx--;
    } else if (cy > 0) {
        int plen = (int)strlen(buf[cy-1]);
        int clen = (int)strlen(buf[cy]);
        if (plen + clen <= NP_COLS) {
            for (int i = 0; i <= clen; i++) buf[cy-1][plen+i] = buf[cy][i];
            for (int i = cy; i+1 < nlines; i++) np_row_copy(i, i+1);
            buf[--nlines][0] = '\0';
            cy--; cx = plen;
        }
    }
    np_scroll_fix();
}

static void np_enter(void) {
    if (nlines >= NP_ROWS) return;
    for (int i = nlines; i > cy+1; i--) np_row_copy(i, i-1);
    int tail = (int)strlen(buf[cy]) - cx;
    for (int i = 0; i <= tail; i++) buf[cy+1][i] = buf[cy][cx+i];
    buf[cy][cx] = '\0'; nlines++; cy++; cx = 0;
    np_scroll_fix();
}

static void np_save(void) {
    fat16_fs_t *fs = kapp_get_fs();
    if (!fs) { strncpy(status, "No filesystem", 47); return; }
    static char flat[NP_ROWS * (NP_COLS + 2)];
    int pos = 0;
    for (int r = 0; r < nlines; r++) {
        int l = (int)strlen(buf[r]);
        for (int j = 0; j < l && pos < (int)sizeof(flat)-2; j++) flat[pos++] = buf[r][j];
        if (r+1 < nlines) flat[pos++] = '\n';
    }
    flat[pos] = '\0';
    int rc = fat16_write_file(fs, 0, fname, flat, (uint32_t)pos);
    strncpy(status, rc == FAT16_OK ? "Saved." : "Save failed.", 47);
}

static void np_load(void) {
    fat16_fs_t *fs = kapp_get_fs();
    if (!fs) { strncpy(status, "No filesystem", 47); return; }
    static char flat[NP_ROWS * (NP_COLS + 2)];
    uint32_t olen = 0;
    if (fat16_read_file(fs, 0, fname, flat, sizeof(flat)-1, &olen) != FAT16_OK) {
        strncpy(status, "Not found.", 47); return;
    }
    flat[olen] = '\0';
    np_reset();
    int row = 0, col = 0;
    for (uint32_t i = 0; i < olen && row < NP_ROWS; i++) {
        if (flat[i] == '\n') { buf[row++][col] = '\0'; col = 0; }
        else if (flat[i] != '\r' && col < NP_COLS) buf[row][col++] = flat[i];
    }
    buf[row][col] = '\0'; nlines = row + 1;
    strncpy(status, "Loaded.", 47);
}

void notepad_create(int wi)  { (void)wi; np_reset(); fname[0]='\0'; status[0]='\0'; fname_mode=0; }
void notepad_destroy(int wi) { (void)wi; }
void notepad_tick(int wi)    { (void)wi; }
void notepad_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void notepad_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void notepad_key(int wi, int kt, char ch) {
    (void)wi;

    if (fname_mode) {
        if (kt == KEY_EVENT_ENTER) {
            fname_tmp[fname_tmp_len] = '\0';
            strncpy(fname, fname_tmp, 31);
            fname_mode = 0;
            if (fmode_save) np_save(); else np_load();
        } else if (kt == KEY_EVENT_BACKSPACE && fname_tmp_len > 0) {
            fname_tmp[--fname_tmp_len] = '\0';
        } else if (kt == KEY_EVENT_CHAR && ch >= 32 && ch <= 126 && fname_tmp_len < 31) {
            fname_tmp[fname_tmp_len++] = ch;
            fname_tmp[fname_tmp_len]   = '\0';
        }
        return;
    }

    switch (kt) {
    case KEY_EVENT_CHAR:
        if (ch == 19) { /* Ctrl+S */
            if (!fname[0]) { fname_mode=1; fmode_save=1; fname_tmp_len=0; fname_tmp[0]='\0';
                strncpy(status,"Filename to save:",47); }
            else np_save();
        } else if (ch == 15) { /* Ctrl+O */
            fname_mode=1; fmode_save=0; fname_tmp_len=0; fname_tmp[0]='\0';
            strncpy(status,"Filename to open:",47);
        } else if (ch >= 32 && ch <= 126) {
            np_insert(ch);
        }
        break;
    case KEY_EVENT_ENTER:     np_enter(); break;
    case KEY_EVENT_BACKSPACE: np_bs(); break;
    case KEY_EVENT_LEFT:
        if (cx > 0) cx--;
        else if (cy > 0) { cy--; cx = (int)strlen(buf[cy]); }
        np_scroll_fix(); break;
    case KEY_EVENT_RIGHT:
        { int l = (int)strlen(buf[cy]);
          if (cx < l) cx++;
          else if (cy+1 < nlines) { cy++; cx=0; }
          np_scroll_fix(); } break;
    case KEY_EVENT_UP:
        if (cy > 0) { cy--; int l=(int)strlen(buf[cy]); if(cx>l) cx=l; np_scroll_fix(); } break;
    case KEY_EVENT_DOWN:
        if (cy+1 < nlines) { cy++; int l=(int)strlen(buf[cy]); if(cx>l) cx=l; np_scroll_fix(); } break;
    default: break;
    }
}

void notepad_render(int wi, int cx2, int cy2, int cw, int ch) {
    (void)wi;
    kd_fill(cx2, cy2, cw, ch, KA_BG);

    /* Header */
    kd_fill(cx2, cy2, cw, 18, KA_HEADBG);
    kd_str(cx2 + 6, cy2 + 5, "Notepad", KA_HEADFG, KA_HEADBG);
    kd_str(cx2 + cw - 136, cy2 + 5, fname[0] ? fname : "(new)", KA_DIM, KA_HEADBG);

    /* Filename prompt */
    if (fname_mode) {
        kd_fill(cx2, cy2 + 18, cw, 14, 0x0A2010U);
        char fb2[48]; int fi = 0;
        const char *lbl = fmode_save ? "Save: " : "Open: ";
        while (*lbl) fb2[fi++] = *lbl++;
        for (int i = 0; fname_tmp[i] && fi < 47; i++) fb2[fi++] = fname_tmp[i];
        fb2[fi++] = '_'; fb2[fi] = '\0';
        kd_str(cx2 + 6, cy2 + 22, fb2, KA_BRIGHT, 0x0A2010U);
    }

    int oy = cy2 + (fname_mode ? 34 : 20);

    /* Text lines */
    for (int r = 0; r < NP_VIS && oy + r*10 + 10 <= cy2 + ch - 14; r++) {
        int li = scroll + r;
        int ty = oy + r * 10;
        if (li < nlines) {
            kd_str(cx2 + 6, ty, buf[li], KA_TEXT, KA_BG);
            if (li == cy) {
                char cc[2] = { buf[cy][cx] ? buf[cy][cx] : ' ', '\0' };
                kd_str(cx2 + 6 + cx * 8, ty, cc, KA_BG, KA_BRIGHT);
            }
        }
    }

    /* Status bar */
    kd_fill(cx2, cy2 + ch - 14, cw, 14, KA_HEADBG);
    if (status[0]) kd_str(cx2 + 6, cy2 + ch - 9, status, KA_BRIGHT, KA_HEADBG);
    else           kd_str(cx2 + 6, cy2 + ch - 9, "^S save  ^O open", KA_DIM, KA_HEADBG);
}
