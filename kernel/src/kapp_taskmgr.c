#include "kapp.h"
#include "process.h"
#include "string.h"

/* ================================================================
 * Task Manager
 * ================================================================ */

static int tm_sel = 0;

static const char *state_str(int s) {
    if (s == PROC_RUNNING)  return "RUN ";
    if (s == PROC_RUNNABLE) return "RBLE";
    if (s == PROC_ZOMBIE)   return "ZOMB";
    if (s == PROC_BLOCKED)  return "BLKD";
    if (s == PROC_SLEEPING) return "SLEP";
    if (s == PROC_STOPPED)  return "STOP";
    return "    ";
}

void taskmgr_create(int wi)  { (void)wi; tm_sel = 0; }
void taskmgr_destroy(int wi) { (void)wi; }
void taskmgr_tick(int wi)    { (void)wi; }
void taskmgr_click(int wi, int x, int y) { (void)wi;(void)x;(void)y; }
void taskmgr_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }

void taskmgr_key(int wi, int kt, char ch) {
    (void)wi; (void)ch;
    /* count live */
    int cnt = 0;
    for (int i = 0; i < MAX_PROCS; i++)
        if (proc_table[i].state != PROC_DEAD) cnt++;

    if (kt == KEY_EVENT_UP   && tm_sel > 0)       tm_sel--;
    if (kt == KEY_EVENT_DOWN && tm_sel < cnt - 1)  tm_sel++;

    if ((ch == 'k' || ch == 'K') && cnt > 0) {
        int idx = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (proc_table[i].state == PROC_DEAD) continue;
            if (idx == tm_sel) { proc_send_signal(proc_table[i].pid, 9); break; }
            idx++;
        }
        if (tm_sel > 0 && tm_sel >= cnt - 1) tm_sel--;
    }
}

void taskmgr_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;

    kd_fill(cx, cy, cw, ch, KA_BG);

    /* Header */
    kd_fill(cx, cy, cw, 18, KA_HEADBG);
    kd_str(cx + 6, cy + 5, "Task Manager", KA_HEADFG, KA_HEADBG);

    /* Column headers */
    int y = cy + 22;
    kd_fill(cx, y, cw, 12, 0x050E08U);
    kd_str(cx +  6, y + 2, "PID",   KA_DIM, 0x050E08U);
    kd_str(cx + 36, y + 2, "NAME",  KA_DIM, 0x050E08U);
    kd_str(cx +116, y + 2, "STATE", KA_DIM, 0x050E08U);
    kd_str(cx +160, y + 2, "PPID",  KA_DIM, 0x050E08U);
    y += 14;

    int row = 0;
    char buf[12];
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == PROC_DEAD) continue;

        int sel = (row == tm_sel);
        uint32_t bg = sel ? KA_SELBG : KA_BG;
        uint32_t fg = sel ? KA_SELFG : KA_TEXT;

        kd_fill(cx, y, cw, 12, bg);

        kd_itoa(proc_table[i].pid, buf, sizeof(buf));
        kd_str(cx +  6, y + 2, buf, fg, bg);

        kd_str_n(cx + 36, y + 2, proc_table[i].name, 9, fg, bg);

        kd_str(cx + 116, y + 2, state_str(proc_table[i].state), fg, bg);

        kd_itoa(proc_table[i].parent_pid, buf, sizeof(buf));
        kd_str(cx + 160, y + 2, buf, fg, bg);

        kd_hline(cx, y + 12, cw, 0x0D1A0DU);
        y += 13;
        row++;
        if (y + 13 > cy + ch - 14) break;
    }

    /* Footer */
    kd_fill(cx, cy + ch - 14, cw, 14, KA_HEADBG);
    kd_str(cx + 6, cy + ch - 9, "Up/Down: select   K: kill", KA_DIM, KA_HEADBG);
}
