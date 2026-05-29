#include "kapp.h"
#include "pit.h"
#include "kmalloc.h"
#include "process.h"
#include "string.h"

/* ================================================================
 * System Information
 * ================================================================ */

void sysinfo_create(int wi)  { (void)wi; }
void sysinfo_destroy(int wi) { (void)wi; }
void sysinfo_key(int wi, int kt, char ch) { (void)wi;(void)kt;(void)ch; }
void sysinfo_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void sysinfo_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }
void sysinfo_tick(int wi) { (void)wi; }

static void row(int cx, int y, const char *label, const char *val) {
    kd_str(cx + 6,   y, label, KA_DIM,    KA_BG);
    kd_str(cx + 140, y, val,   KA_BRIGHT, KA_BG);
}

static void bar(int cx, int y, int w, uint32_t val, uint32_t max) {
    kd_fill(cx, y, w, 8, 0x0A0A0AU);
    kd_rect(cx, y, w, 8, KA_BORDER);
    if (max > 0 && val > 0) {
        uint32_t pct = (val * 1000u) / max;
        int bw = (int)((uint32_t)(w - 2) * pct / 1000u);
        if (bw > 0) kd_fill(cx + 1, y + 1, bw, 6, 0x00CC55U);
    }
}

void sysinfo_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, KA_BG);

    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "System Information", KA_HEADFG, KA_HEADBG);

    int y = cy + 26;
    char buf[24];

    kd_str(cx + 6, y, "-- CPU --", KA_BRIGHT, KA_BG); y += 12;
    row(cx, y, "Arch:",    "i686 32-bit x86"); y += 12;
    row(cx, y, "Mode:",    "Protected Mode");  y += 12;
    row(cx, y, "Timer:",   "PIT 100 Hz");      y += 12;

    kd_utoa(pit_ticks(), buf, sizeof(buf));
    row(cx, y, "Ticks:", buf); y += 12;

    uint32_t s = pit_ticks() / 100u;
    uint32_t hh = s/3600, mm=(s%3600)/60, ss=s%60;
    char upbuf[20]; int ui=0;
    char t[8];
    kd_utoa(hh,t,8); for(int j=0;t[j];j++) upbuf[ui++]=t[j]; upbuf[ui++]='h'; upbuf[ui++]=' ';
    kd_utoa(mm,t,8); for(int j=0;t[j];j++) upbuf[ui++]=t[j]; upbuf[ui++]='m'; upbuf[ui++]=' ';
    kd_utoa(ss,t,8); for(int j=0;t[j];j++) upbuf[ui++]=t[j]; upbuf[ui++]='s'; upbuf[ui]='\0';
    row(cx, y, "Uptime:", upbuf); y += 16;

    kd_hline(cx+6, y, cw-12, KA_BORDER); y += 8;
    kd_str(cx + 6, y, "-- Memory --", KA_BRIGHT, KA_BG); y += 12;

    uint32_t used = kmalloc_used(), total = kmalloc_total();
    kd_utoa(used/1024,  buf, sizeof(buf));
    row(cx, y, "Heap used (KB):", buf); y += 12;
    kd_utoa(total/1024, buf, sizeof(buf));
    row(cx, y, "Heap total (KB):", buf); y += 10;
    bar(cx + 6, y, cw - 12, used, total); y += 14;

    kd_hline(cx+6, y, cw-12, KA_BORDER); y += 8;
    kd_str(cx + 6, y, "-- Processes --", KA_BRIGHT, KA_BG); y += 12;

    int np = 0;
    for (int i = 0; i < MAX_PROCS; i++)
        if (proc_table[i].state != PROC_DEAD) np++;
    kd_itoa(np, buf, sizeof(buf));
    row(cx, y, "Active:", buf); y += 12;
    kd_itoa(MAX_PROCS, buf, sizeof(buf));
    row(cx, y, "Max slots:", buf);
}
