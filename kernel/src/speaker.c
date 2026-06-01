/*
 * speaker.c — PC speaker driver using PIT channel 2 and port 0x61.
 *
 * Async playback: speaker_tick() is called from pit_timer_tick() at 100 Hz.
 * Each note is played for (duration - NOTE_GAP_MS) then silenced briefly
 * before the next note begins, giving a natural staccato separation.
 *
 * F12 triggers speaker_play_anthem() which queues the Star-Spangled Banner.
 * Pressing F12 while the anthem is already playing is a no-op.
 */

#include "io.h"
#include "pit.h"
#include "serial.h"
#include "speaker.h"
#include "types.h"

/* PIT channel 2 + speaker ports */
#define PIT_CH2      0x42
#define PIT_CMD_CH2  0x43
#define SPKR_CTRL    0x61

/* Command byte: channel 2, lobyte/hibyte, mode 3 (square wave), binary */
#define PIT_CH2_MODE3 0xB6

/* Gap of silence between consecutive notes (ms) */
#define NOTE_GAP_MS 35

/* ------------------------------------------------------------------ */
/* Note frequency constants (Hz) — G major scale                       */
/* ------------------------------------------------------------------ */
#define REST 0
#define G3 196
#define A3 220
#define B3 247
#define C4 262
#define D4 294
#define E4 330
#define FS4 370
#define G4 392
#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define E5 659

/* Duration constants at ~80 BPM (ms) */
#define N_S   187   /* sixteenth note  */
#define N_E   375   /* eighth note     */
#define N_DE  562   /* dotted eighth   */
#define N_Q   750   /* quarter note    */
#define N_DQ 1125   /* dotted quarter  */
#define N_H  1500   /* half note       */
#define N_DH 2250   /* dotted half     */

/* ------------------------------------------------------------------ */
/* Song table: The Star-Spangled Banner, verse 1, key of G major, 3/4  */
/*                                                                      */
/* Pickup beat leads into m1.  Each measure is 3 quarter-note beats.   */
/* Melody follows the traditional Francis Scott Key / John Stafford    */
/* Smith setting.                                                       */
/* ------------------------------------------------------------------ */
static const struct note anthem[] = {

    /* ---- Pickup: "Oh say" ---- */
    {D4,  N_DE}, {B3,  N_S},

    /* ---- m1-m4: "can you see, by the dawn's early light," ---- */
    {G3,  N_Q},  {B3,  N_Q},  {D4,  N_Q},    /* can  you  see     */
    {G4,  N_H},  {B4,  N_DE}, {A4,  N_S},    /* see(held) by the  */
    {G4,  N_Q},  {E4,  N_Q},  {D4,  N_Q},    /* dawn's ear-ly     */
    {D4,  N_H},  {D4,  N_DE}, {B3,  N_S},    /* light(held) what-so */

    /* ---- m5-m8: "What so proudly we hailed at the twilight's last gleaming?" ---- */
    {G3,  N_DQ}, {B3,  N_E},  {D4,  N_Q},    /* proud-ly  we      */
    {G4,  N_H},  {B4,  N_DE}, {A4,  N_S},    /* hailed(held) at-the */
    {G4,  N_Q},  {E4,  N_Q},  {D4,  N_Q},    /* twi-light's last  */
    {E4,  N_Q},  {D4,  N_Q},  {D4,  N_DE},   /* gleam-  ing  (what-) */
    {B3,  N_S},                               /* (-so / Whose broad) */

    /* ---- m9-m12: "Whose broad stripes and bright stars, through the perilous fight," ---- */
    {G3,  N_Q},  {B3,  N_Q},  {D4,  N_Q},    /* stripes and bright */
    {G4,  N_H},  {B4,  N_DE}, {A4,  N_S},    /* stars(held) thro'-the */
    {G4,  N_Q},  {E4,  N_Q},  {D4,  N_Q},    /* per-  il- ous     */
    {D4,  N_H},  {D4,  N_DE}, {B3,  N_S},    /* fight(held) o'er-the */

    /* ---- m13-m16: "O'er the ramparts we watched, were so gallantly streaming?" ---- */
    {G3,  N_DQ}, {B3,  N_E},  {D4,  N_Q},    /* ram- parts  we    */
    {G4,  N_H},  {B4,  N_DE}, {A4,  N_S},    /* watched(held) were-so */
    {G4,  N_Q},  {E4,  N_Q},  {D4,  N_Q},    /* gal- lant- ly     */
    {E4,  N_Q},  {D4,  N_Q},  {D4,  N_E},    /* stream- ing (And-) */
    {B3,  N_E},                               /* (-the)            */

    /* ---- m17-m20: "And the rockets' red glare, the bombs bursting in air," ---- */
    {E5,  N_Q},  {D5,  N_Q},  {B4,  N_Q},    /* rock- ets'  red   */
    {G4,  N_H},  {E5,  N_E},  {D5,  N_E},    /* glare(held) the-bombs */
    {C5,  N_Q},  {A4,  N_Q},  {FS4, N_Q},    /* burst- ing   in   */
    {D4,  N_H},  {D4,  N_Q},                 /* air(held)  gave   */

    /* ---- m21-m24: "Gave proof through the night that our flag was still there." ---- */
    {G3,  N_DQ}, {B3,  N_E},  {D4,  N_Q},    /* proof thro'  the  */
    {G4,  N_H},  {B4,  N_DE}, {A4,  N_S},    /* night(held) that-our */
    {G4,  N_Q},  {E4,  N_Q},  {D4,  N_Q},    /* flag  was  still  */
    {D4,  N_H},  {D4,  N_Q},                 /* there(held)  O    */

    /* ---- m25-m28: "O say does that star-spangled banner yet wave" ---- */
    /* The traditional melody rises: G A B D / C B A / A G (held) */
    {G3,  N_E},  {A3,  N_E},  {B3,  N_Q},  {D4,  N_Q},   /* say does  that    */
    {C4,  N_E},  {B3,  N_E},  {A3,  N_Q},                 /* star spang- led   */
    {A3,  N_DQ}, {B3,  N_E},  {C4,  N_Q},                 /* ban-  ner  yet    */
    {C4,  N_DH},                                           /* wave (held/fermata) */

    /* ---- m29-m31: "O'er the land of the free," ---- */
    {D4,  N_DE}, {E4,  N_S},  {FS4, N_Q},    /* o'er  the  (land) */
    {G4,  N_DQ}, {A4,  N_E},  {B4,  N_Q},    /* land  of   the    */
    {D5,  N_H},  {C5,  N_E},  {B4,  N_E},    /* FREE(peak) and-the */

    /* ---- m32-m33: "and the home of the brave." ---- */
    {A4,  N_DQ}, {G4,  N_E},  {FS4, N_Q},    /* home  of   the    */
    {G4,  N_DH},                              /* brave (tonic, fermata) */

    {REST, 0}  /* end-of-song sentinel */
};

/* ------------------------------------------------------------------ */
/* Async playback state                                                 */
/* ------------------------------------------------------------------ */
static const struct note *g_song     = (void*)0;
static int                g_song_len = 0;
static int                g_note_idx = 0;
static uint32_t           g_note_end = 0;  /* pit_ticks() value when current segment ends */
static int                g_playing  = 0;
static int                g_in_gap   = 0;  /* 1 while in the inter-note silence */

/* ------------------------------------------------------------------ */

void speaker_init(void) {
    serial_write(COM1, "[SPK] init\n");
    speaker_off();
}

void speaker_on(uint32_t frequency) {
    serial_write(COM1, "[SPK] on freq=");
    serial_write_dec(COM1, frequency);
    serial_write(COM1, "\n");
    if (frequency == 0) { speaker_off(); return; }

    uint32_t divisor = 1193182u / frequency;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;

    outb(PIT_CMD_CH2, PIT_CH2_MODE3);
    outb(PIT_CH2, (uint8_t)(divisor & 0xFFu));
    outb(PIT_CH2, (uint8_t)((divisor >> 8) & 0xFFu));

    /* Enable speaker: bits 0 (timer gate) and 1 (speaker data) */
    outb(SPKR_CTRL, inb(SPKR_CTRL) | 0x03u);
}

void speaker_off(void) {
    serial_write(COM1, "[SPK] off\n");
    outb(SPKR_CTRL, inb(SPKR_CTRL) & (uint8_t)~0x03u);
}

void speaker_play_note(uint32_t frequency, uint32_t duration_ms) {
    /* Synchronous (busy-wait) helper — not used by the async path */
    speaker_on(frequency);
    uint32_t start = pit_ticks();
    uint32_t dur_ticks = duration_ms / 10u;
    while (pit_ticks() - start < dur_ticks)
        __asm__ volatile("pause");
    speaker_off();
}

/* Start playing the next note (or rest) at g_note_idx. */
static void speaker_start_note(void) {
    while (g_note_idx < g_song_len) {
        const struct note *n = &g_song[g_note_idx];
        if (n->duration_ms == 0 && n->frequency == REST) {
            /* End sentinel */
            serial_write(COM1, "[SPK] song complete\n");
            speaker_off();
            g_playing = 0;
            return;
        }

        uint32_t play_ms = (n->duration_ms > NOTE_GAP_MS)
                           ? (uint32_t)(n->duration_ms - NOTE_GAP_MS)
                           : (uint32_t)n->duration_ms;
        uint32_t play_ticks = play_ms / 10u;
        if (play_ticks == 0) play_ticks = 1;

        if (n->frequency == REST) {
            serial_write(COM1, "[SPK] rest dur=");
            serial_write_dec(COM1, n->duration_ms);
            serial_write(COM1, "\n");
            speaker_off();
        } else {
            serial_write(COM1, "[SPK] note ");
            serial_write_dec(COM1, (uint32_t)g_note_idx);
            serial_write(COM1, " freq=");
            serial_write_dec(COM1, n->frequency);
            serial_write(COM1, " dur=");
            serial_write_dec(COM1, n->duration_ms);
            serial_write(COM1, "\n");
            speaker_on(n->frequency);
        }
        g_in_gap   = 0;
        g_note_end = pit_ticks() + play_ticks;
        return;
    }
    serial_write(COM1, "[SPK] song complete\n");
    speaker_off();
    g_playing = 0;
}

void speaker_play_song(const struct note *notes, int count) {
    if (g_playing) return;
    serial_write(COM1, "[SPK] song start\n");
    g_song     = notes;
    g_song_len = count;
    g_note_idx = 0;
    g_playing  = 1;
    g_in_gap   = 0;
    speaker_start_note();
}

void speaker_play_anthem(void) {
    serial_write(COM1, "[SPK] anthem requested\n");
    /* Compute length: entries up to (and including) the {0,0} sentinel */
    int len = 0;
    while (anthem[len].duration_ms != 0 || anthem[len].frequency != REST)
        len++;
    len++; /* include sentinel so speaker_start_note() sees it */
    speaker_play_song(anthem, len);
}

/* Called every 10 ms from pit_timer_tick().  Non-blocking. */
void speaker_tick(void) {
    if (!g_playing) return;

    uint32_t t = pit_ticks();
    if (t < g_note_end) return;  /* current segment not yet finished */

    if (g_in_gap) {
        /* Gap ended — move to next note */
        g_in_gap = 0;
        g_note_idx++;
        speaker_start_note();
    } else {
        /* Note ended — start inter-note silence */
        speaker_off();
        g_in_gap   = 1;
        g_note_end = t + (NOTE_GAP_MS / 10u);
        if (g_note_end == t) g_note_end = t + 1u;  /* at least 1 tick */
    }
}