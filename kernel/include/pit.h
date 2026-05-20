#ifndef SIMPLE_PIT_H
#define SIMPLE_PIT_H

#include "registers.h"
#include "types.h"

/*
 * PIT channel 0 drives IRQ0 at PIT_HZ ticks per second.
 * PIT_BASE_FREQ / PIT_HZ = 11931 ≈ 100 Hz (actual: 1193182/11931 ≈ 100.00 Hz).
 */
#define PIT_HZ        100U
#define PIT_BASE_FREQ 1193182U
#define PIT_DIVISOR   (PIT_BASE_FREQ / PIT_HZ)

/* I/O ports */
#define PIT_CHAN0 0x40
#define PIT_CMD   0x43

void     pit_init(void);
uint32_t pit_ticks(void);

/*
 * Called from the IRQ0 dispatch in isr_handler().
 * Increments the tick counter, sends EOI, and invokes proc_timer_tick()
 * to perform time-slice preemption when appropriate.
 */
void pit_timer_tick(registers_t *regs);

#endif
