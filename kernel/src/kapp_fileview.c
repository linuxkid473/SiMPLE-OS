#include "kapp.h"
#include "fat16.h"
#include "string.h"

/* ================================================================
 * File Viewer — read-only, scrollable
 * ================================================================ */

#define FV_MAXL  100
#define FV_LINEW  68
#define FV_VIS    20

static char fv_lines[FV_MAXL][FV_LINEW + 1];
static int  fv_n, fv_scroll;
static char fv_fname[32];
static int  fv_fname_mode, fv_fname_len;
static char fv_status[48];

static void fv_clear(void) {
    for (int i = 0; i < FV_MAXL; i++) fv_lines[i][0] = '\0';
    fv_n = 0; fv_scroll = 0;
}

static void fv_load(void) {
    fv_clear();
    fat16_fs_t *fs = kapp_get_fs();
    if (!fs || !fv_fname[0]) { strncpy(fv_status,"No FS or filename.",47); return; }
    static char raw[FV_MAXL * (FV_LINEW + 2)];
    uint32_t olen = 0;
    if (fat16_read_file(fs, 0, fv_fname, raw, sizeof(raw)-1, &olen) != FAT16_OK) {
        strncpy(fv_status,"Not found.",47); return;
    }
    raw[olen] = '\0';
    int row = 0, col = 0;
    for (uint32_t i = 0; i < olen && row < FV_MAXL; i++) {
        if (raw[i] == '\n') { fv_lines[row++][col]='\0'; col=0; }
        else if (raw[i] != '\r' && col < FV_LINEW) fv_lines[row][col++] = raw[i];
    }
    fv_lines[row][col]='\0'; fv_n = row + 1;
    strncpy(fv_status,"Loaded.",47);
}

void fileview_create(int wi)  { (void)wi; fv_clear(); fv_fname[0]='\0'; fv_status[0]='\0'; fv_fname_mode=0; fv_fname_len=0; }
void fileview_destroy(int wi) { (void)wi; fv_clear(); }
void fileview_tick(int wi)    { (void)wi; }
void fileview_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void fileview_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void fileview_key(int wi, int kt, char ch) {
    (void)wi;
    if (fv_fname_mode) {
        if (kt == KEY_EVENT_ENTER) { fv_fname[fv_fname_len]='\0'; fv_fname_mode=0; fv_load(); }
        else if (kt == KEY_EVENT_BACKSPACE && fv_fname_len > 0) fv_fname[--fv_fname_len]='\0';
        else if (kt == KEY_EVENT_CHAR && ch>=32 && ch<=126 && fv_fname_len<31)
            { fv_fname[fv_fname_len++]=ch; fv_fname[fv_fname_len]='\0'; }
        return;
    }
    if (kt == KEY_EVENT_UP   && fv_scroll > 0)                     fv_scroll--;
    if (kt == KEY_EVENT_DOWN && fv_scroll + FV_VIS < fv_n)          fv_scroll++;
    if (ch == ' ')  { fv_scroll += FV_VIS; if (fv_scroll + FV_VIS > fv_n) fv_scroll = fv_n - FV_VIS; if (fv_scroll < 0) fv_scroll = 0; }
    if (ch == 'b' || ch == 'B') { fv_scroll -= FV_VIS; if (fv_scroll < 0) fv_scroll = 0; }
    if (ch == 'o' || ch == 'O') { fv_fname_mode=1; fv_fname_len=0; fv_fname[0]='\0'; strncpy(fv_status,"Filename:",47); }
    if (ch == 'r' || ch == 'R') fv_load();
}

void fileview_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, KA_BG);

    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "File Viewer", KA_HEADFG, KA_HEADBG);
    kd_str(cx + cw - 136, cy + 5, fv_fname[0] ? fv_fname : "(none)", KA_DIM, KA_HEADBG);

    int oy = cy + 20;
    if (fv_fname_mode) {
        kd_fill(cx, oy, cw, 14, 0x0A2010U);
        char fb[48] = "Open: "; int fi = 6;
        for (int i = 0; fv_fname[i] && fi < 47; i++) fb[fi++] = fv_fname[i];
        fb[fi++] = '_'; fb[fi] = '\0';
        kd_str(cx + 6, oy + 3, fb, KA_BRIGHT, 0x0A2010U);
        oy += 16;
    }

    for (int r = 0; r < FV_VIS && oy + r*10 + 10 <= cy + ch - 14; r++) {
        int li = fv_scroll + r;
        if (li >= fv_n) break;
        char lb[4]; kd_itoa(li+1, lb, sizeof(lb));
        kd_str(cx + 2, oy + r*10, lb, KA_DIM, KA_BG);
        kd_str(cx + 26, oy + r*10, fv_lines[li], KA_TEXT, KA_BG);
    }

    kd_fill(cx, cy + ch - 14, cw, 14, KA_HEADBG);
    kd_str(cx + 6, cy + ch - 9,
           fv_status[0] ? fv_status : "O:open  R:reload  Up/Dn:scroll  Spc/B:page",
           fv_status[0] ? KA_BRIGHT : KA_DIM, KA_HEADBG);
}
