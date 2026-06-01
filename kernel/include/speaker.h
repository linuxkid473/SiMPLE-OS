#ifndef SIMPLE_SPEAKER_H
#define SIMPLE_SPEAKER_H

#include "types.h"

struct note {
    uint16_t frequency;   /* Hz; 0 = rest */
    uint16_t duration_ms; /* 0 = end-of-song sentinel */
};

void speaker_init(void);
void speaker_on(uint32_t frequency);
void speaker_off(void);
void speaker_play_note(uint32_t frequency, uint32_t duration_ms);
void speaker_play_song(const struct note *notes, int count);
void speaker_play_anthem(void);
void speaker_tick(void);  /* called from pit_timer_tick() every 10 ms */

#endif
