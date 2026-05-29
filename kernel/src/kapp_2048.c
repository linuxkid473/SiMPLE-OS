#include "kapp.h"
#include "string.h"

/* 2048 — 4×4 grid, arrow-key slide/merge */
#define G        4
#define CELL_SZ  52
#define CELL_GAP  4
#define GRID_OFF_X 8
#define GRID_OFF_Y 24

static int     g_grid[G][G];
static int     g_score, g_best, g_over, g_won;
static uint32_t g_seed;

static void g_add_tile(void) {
    int ex[G*G], ey[G*G], n = 0;
    for (int r = 0; r < G; r++)
        for (int c = 0; c < G; c++)
            if (!g_grid[r][c]) { ex[n] = r; ey[n] = c; n++; }
    if (!n) return;
    g_seed = g_seed * 1664525u + 1013904223u;
    int idx = (int)((g_seed >> 8) % (uint32_t)n);
    g_grid[ex[idx]][ey[idx]] = ((g_seed >> 3) & 7) ? 2 : 4;
}

static void g_new_game(void) {
    for (int i = 0; i < G*G; i++) ((int *)g_grid)[i] = 0;
    g_score = 0; g_over = 0; g_won = 0;
    g_add_tile(); g_add_tile();
}

void g2048_create(int wi)  { (void)wi; g_best = 0; g_seed = 8675309u; g_new_game(); }
void g2048_destroy(int wi) { (void)wi; }
void g2048_mouse(int wi, int x, int y, int b) { (void)wi;(void)x;(void)y;(void)b; }
void g2048_tick(int wi)    { (void)wi; }

void g2048_click(int wi, int x, int y) {
    (void)wi;
    g_seed ^= (uint32_t)x ^ (uint32_t)y;
    if (g_over || g_won) g_new_game();
}

/* Slide one row left; returns 1 if the board changed */
static int slide_left(int row[G]) {
    /* Step 1: compact (remove gaps) */
    int tmp[G] = {0};
    int j = 0;
    for (int i = 0; i < G; i++) if (row[i]) tmp[j++] = row[i];
    /* Step 2: merge adjacent equals (each tile merges at most once) */
    for (int i = 0; i < G - 1; i++) {
        if (tmp[i] && tmp[i] == tmp[i+1]) {
            tmp[i] <<= 1;
            g_score += tmp[i];
            if (g_score > g_best) g_best = g_score;
            if (tmp[i] == 2048) g_won = 1;
            tmp[i+1] = 0;
            i++;  /* skip merged tile */
        }
    }
    /* Step 3: compact again */
    int out[G] = {0};
    j = 0;
    for (int i = 0; i < G; i++) if (tmp[i]) out[j++] = tmp[i];
    int moved = 0;
    for (int i = 0; i < G; i++) { if (out[i] != row[i]) moved = 1; row[i] = out[i]; }
    return moved;
}

void g2048_key(int wi, int kt, char ch) {
    (void)wi;
    g_seed ^= (uint32_t)kt;
    if (g_over || g_won) {
        if (ch == ' ' || ch == 'r' || ch == 'R') g_new_game();
        return;
    }
    int moved = 0;
    int row[G];

    if (kt == KEY_EVENT_LEFT) {
        for (int r = 0; r < G; r++) {
            for (int c = 0; c < G; c++) row[c] = g_grid[r][c];
            moved |= slide_left(row);
            for (int c = 0; c < G; c++) g_grid[r][c] = row[c];
        }
    } else if (kt == KEY_EVENT_RIGHT) {
        for (int r = 0; r < G; r++) {
            for (int c = 0; c < G; c++) row[c] = g_grid[r][G-1-c];
            moved |= slide_left(row);
            for (int c = 0; c < G; c++) g_grid[r][G-1-c] = row[c];
        }
    } else if (kt == KEY_EVENT_UP) {
        for (int c = 0; c < G; c++) {
            for (int r = 0; r < G; r++) row[r] = g_grid[r][c];
            moved |= slide_left(row);
            for (int r = 0; r < G; r++) g_grid[r][c] = row[r];
        }
    } else if (kt == KEY_EVENT_DOWN) {
        for (int c = 0; c < G; c++) {
            for (int r = 0; r < G; r++) row[r] = g_grid[G-1-r][c];
            moved |= slide_left(row);
            for (int r = 0; r < G; r++) g_grid[G-1-r][c] = row[r];
        }
    } else {
        return;
    }

    if (moved) g_add_tile();

    if (!g_won) {
        /* Check for any move remaining */
        int can = 0;
        for (int r = 0; r < G && !can; r++)
            for (int c = 0; c < G && !can; c++) {
                if (!g_grid[r][c])                           { can = 1; break; }
                if (c+1 < G && g_grid[r][c]==g_grid[r][c+1]) { can = 1; break; }
                if (r+1 < G && g_grid[r][c]==g_grid[r+1][c]) { can = 1; break; }
            }
        if (!can) g_over = 1;
    }
}

static uint32_t tile_bg(int v) {
    switch (v) {
        case    2: return 0x1A3A26u;
        case    4: return 0x1A5533u;
        case    8: return 0x22AA55u;
        case   16: return 0x44DD77u;
        case   32: return 0xFF8833u;
        case   64: return 0xFF4422u;
        case  128: return 0xFFCC00u;
        case  256: return 0xFFFF33u;
        case  512: return 0x44AAFFu;
        case 1024: return 0x4444FFu;
        case 2048: return 0xFF44FFu;
        default:   return 0x888888u;
    }
}

static uint32_t tile_fg(int v) {
    return (v <= 8) ? 0xAAFFBBu : 0xFFFFFFu;
}

void g2048_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;
    kd_fill(cx, cy, cw, ch, 0x020604);

    /* Header */
    kd_fill(cx, cy, cw, 18, 0x001208);
    kd_str(cx + 4,   cy + 5, "2048",   KA_HEADFG, 0x001208);
    kd_str(cx + 60,  cy + 5, "Score:", KA_DIM,    0x001208);
    char b[12];
    kd_itoa(g_score, b, 12);
    kd_str(cx + 108, cy + 5, b,         KA_BRIGHT, 0x001208);
    kd_str(cx + 166, cy + 5, "Best:",  KA_DIM,    0x001208);
    kd_itoa(g_best, b, 12);
    kd_str(cx + 210, cy + 5, b,         KA_YELLOW, 0x001208);

    /* Grid background */
    int gx = cx + GRID_OFF_X;
    int gy = cy + GRID_OFF_Y;
    int gsz = G * CELL_SZ + (G + 1) * CELL_GAP;
    kd_fill(gx, gy, gsz, gsz, 0x0A1A10u);
    kd_rect(gx - 1, gy - 1, gsz + 2, gsz + 2, 0x1A5533u);

    /* Tiles */
    for (int r = 0; r < G; r++) {
        for (int c = 0; c < G; c++) {
            int tx = gx + CELL_GAP + c * (CELL_SZ + CELL_GAP);
            int ty = gy + CELL_GAP + r * (CELL_SZ + CELL_GAP);
            int  v = g_grid[r][c];
            uint32_t bg = v ? tile_bg(v) : 0x071007u;
            kd_fill(tx, ty, CELL_SZ, CELL_SZ, bg);
            if (v) {
                /* gloss stripe at top */
                kd_fill(tx, ty, CELL_SZ, 3, 0xFFFFFF10u | (bg >> 1 & 0x7F7F7Fu));
                kd_itoa(v, b, 12);
                int len = (int)strlen(b);
                int tx2 = tx + (CELL_SZ - len * 8) / 2;
                int ty2 = ty + (CELL_SZ - 8) / 2;
                kd_str(tx2, ty2, b, tile_fg(v), bg);
            }
        }
    }

    /* Hint */
    kd_str(cx + 4, cy + ch - 10, "Arrow keys to slide tiles", KA_DIM, 0x020604);

    /* Overlay */
    if (g_won || g_over) {
        uint32_t oc = g_won ? 0x44FF88u : KA_RED;
        int ox = cx + 24, oy = cy + 96;
        kd_fill(ox, oy, 212, 48, 0x060E07u);
        kd_rect(ox, oy, 212, 48, oc);
        kd_str(ox + 20, oy + 10, g_won ? "YOU REACHED 2048!" : "GAME OVER!",      oc,    0x060E07u);
        kd_str(ox + 20, oy + 24, "Space/R to restart",                        KA_DIM, 0x060E07u);
    }
}
