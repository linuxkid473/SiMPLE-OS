#include "kapp.h"
#include "fat16.h"
#include "string.h"

/* ================================================================
 * File Manager
 * ================================================================ */

#define FM_MAX 40
#define FM_VIS 20

static fat16_dirent_t fm_ents[FM_MAX];
static int   fm_cnt, fm_sel, fm_scroll;
static uint16_t fm_dir;
static char  fm_status[48];

static void fm_load(void) {
    fat16_fs_t *fs = kapp_get_fs();
    fm_cnt = 0;
    if (!fs) return;
    fat16_list_entries(fs, fm_dir, fm_ents, FM_MAX, &fm_cnt);
    if (fm_sel >= fm_cnt) fm_sel = fm_cnt > 0 ? fm_cnt - 1 : 0;
}

void filemgr_create(int wi) { (void)wi; fm_dir=0; fm_sel=0; fm_scroll=0; fm_status[0]='\0'; fm_load(); }
void filemgr_destroy(int wi) { (void)wi; }
void filemgr_tick(int wi)    { (void)wi; }
void filemgr_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void filemgr_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void filemgr_key(int wi, int kt, char ch) {
    (void)wi;
    if (kt == KEY_EVENT_UP   && fm_sel > 0)          fm_sel--;
    if (kt == KEY_EVENT_DOWN && fm_sel < fm_cnt - 1)  fm_sel++;
    if (fm_sel < fm_scroll)           fm_scroll = fm_sel;
    if (fm_sel >= fm_scroll + FM_VIS) fm_scroll = fm_sel - FM_VIS + 1;

    if (kt == KEY_EVENT_ENTER && fm_cnt > 0) {
        fat16_dirent_t *e = &fm_ents[fm_sel];
        if (e->attr & FAT16_ATTR_DIRECTORY) {
            fat16_fs_t *fs = kapp_get_fs();
            if (fs) {
                uint16_t nc = 0;
                if (strncmp(e->name, "..", 2) == 0) { fm_dir=0; }
                else if (fat16_change_dir(fs, fm_dir, e->name, &nc) == FAT16_OK) fm_dir = nc;
                fm_sel = 0; fm_scroll = 0; fm_load();
            }
        }
    }

    if ((ch == 'd' || ch == 'D') && fm_cnt > 0) {
        fat16_dirent_t *e = &fm_ents[fm_sel];
        if (!(e->attr & FAT16_ATTR_DIRECTORY)) {
            fat16_fs_t *fs = kapp_get_fs();
            if (fs && fat16_remove(fs, fm_dir, e->name) == FAT16_OK)
                strncpy(fm_status, "Deleted.", 47);
            else strncpy(fm_status, "Delete failed.", 47);
            fm_load();
        }
    }
    if (ch == 'r' || ch == 'R') { fm_load(); strncpy(fm_status, "Refreshed.", 47); }
}

void filemgr_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, KA_BG);

    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "File Manager", KA_HEADFG, KA_HEADBG);

    int y = cy + 22;
    kd_fill(cx, y, cw, 12, 0x050E08U);
    kd_str(cx +  6, y + 2, "T", KA_DIM, 0x050E08U);
    kd_str(cx + 18, y + 2, "NAME", KA_DIM, 0x050E08U);
    kd_str(cx +cw-56, y + 2, "SIZE", KA_DIM, 0x050E08U);
    y += 14;

    if (fm_cnt == 0) {
        kd_str(cx + 6, y + 2, "(empty)", KA_DIM, KA_BG);
    }

    for (int r = 0; r < FM_VIS && y + 12 <= cy + ch - 14; r++) {
        int idx = r + fm_scroll;
        if (idx >= fm_cnt) break;
        fat16_dirent_t *e = &fm_ents[idx];
        int sel = (idx == fm_sel);
        uint32_t bg = sel ? KA_SELBG : KA_BG;
        uint32_t fg = sel ? KA_SELFG : KA_TEXT;

        kd_fill(cx, y, cw, 12, bg);
        kd_char(cx + 6, y + 2,
                (e->attr & FAT16_ATTR_DIRECTORY) ? 'D' : 'F',
                (e->attr & FAT16_ATTR_DIRECTORY) ? KA_YELLOW : KA_BRIGHT,
                bg);
        kd_str_n(cx + 18, y + 2, e->name, 22, fg, bg);

        if (!(e->attr & FAT16_ATTR_DIRECTORY)) {
            char sb[10]; kd_utoa(e->size, sb, sizeof(sb));
            kd_str(cx + cw - 56, y + 2, sb, KA_DIM, bg);
        }
        kd_hline(cx, y + 12, cw, 0x0D1A0DU);
        y += 13;
    }

    kd_fill(cx, cy + ch - 14, cw, 14, KA_HEADBG);
    kd_str(cx + 6, cy + ch - 9,
           fm_status[0] ? fm_status : "Enter:cd  D:delete  R:refresh",
           fm_status[0] ? KA_BRIGHT : KA_DIM, KA_HEADBG);
}
