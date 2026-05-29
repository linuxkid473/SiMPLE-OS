#include "kapp.h"

/* ================================================================
 * About SiMPLE OS
 * ================================================================ */

void about_create(int wi)  { (void)wi; }
void about_destroy(int wi) { (void)wi; }
void about_key(int wi, int kt, char ch) { (void)wi;(void)kt;(void)ch; }
void about_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void about_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }
void about_tick(int wi) { (void)wi; }

void about_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi; (void)cw; (void)ch;

    kd_fill(cx, cy, cw, ch, KA_BG);

    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "About SiMPLE OS", KA_HEADFG, KA_HEADBG);

    int y = cy + 26;
    kd_str(cx + 6, y,      "SiMPLE OS",              KA_BRIGHT, KA_BG); y += 14;
    kd_str(cx + 6, y,      "Version:   0.9",          KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "Arch:      i686 32-bit",  KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "Memory:    Flat / No MMU",KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "FS:        FAT16",        KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "Display:   VESA linear",  KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "Scheduler: Preemptive RR",KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "Syscalls:  int 0x80",     KA_TEXT,   KA_BG); y += 12;
    kd_str(cx + 6, y,      "Boot:      GRUB2/MB1",    KA_TEXT,   KA_BG); y += 20;

    kd_fill(cx, y, cw, 1, KA_BORDER); y += 8;
    kd_str(cx + 6, y, "A hobby kernel.  No copyright.", KA_DIM, KA_BG);
}
