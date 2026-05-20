/*
 * pit.c — Intel 8253/8254 Programmable Interval Timer driver.
 *
 * Channel 0 is programmed in mode 2 (rate generator), output goes to
 * the master PIC IRQ0 line.  Every time the 16-bit counter rolls over,
 * an IRQ0 is asserted.
 *
 *   Divisor = 1193182 / 100 = 11931  →  ~100 Hz
 *
 * Flow on each tick:
 *   IRQ0 fires → isr32 saves ring3 state → isr_handler dispatches 0x20
 *   → pit_timer_tick() → pic_eoi(0) → proc_timer_tick() (may preempt)
 *   → return up call chain → iret into next (or same) process
 */

#include "io.h"
#include "klog.h"
#include "pic.h"
#include "pit.h"
#include "process.h"
#include "serial.h"
#include "types.h"

/* PIT command byte: channel 0, lobyte/hibyte, mode 2, binary */
#define PIT_MODE2_CH0 0x34

static volatile uint32_t g_ticks = 0;

void pit_init(void) {
    uint16_t divisor = (uint16_t)PIT_DIVISOR;

    outb(PIT_CMD,   PIT_MODE2_CH0);
    outb(PIT_CHAN0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHAN0, (uint8_t)((divisor >> 8) & 0xFF));

    klog_dec("pit", "hz",      PIT_HZ);
    klog_hex("pit", "divisor", (uint32_t)divisor);
}

uint32_t pit_ticks(void) {
    return g_ticks;
}

void pit_timer_tick(registers_t *regs) {
    g_ticks++;

    /*
     * Send EOI before scheduling.  This lets the PIC latch the next IRQ0
     * edge while we're still in the handler (IF=0, so it won't fire yet).
     * The next interrupt will be delivered after iret re-enables IF.
     */
    pic_eoi(0);

    /* Log once per second on the serial port for diagnostics */
    if ((g_ticks % PIT_HZ) == 0) {
        serial_write(COM1, "[pit] uptime=");
        serial_write_dec(COM1, g_ticks / PIT_HZ);
        serial_write(COM1, "s  pid=");
        if (current_proc >= 0)
            serial_write_dec(COM1, (uint32_t)proc_table[current_proc].pid);
        else
            serial_write(COM1, "none");
        serial_write(COM1, "\n");
    }

    /* Hand off to the scheduler — may modify *regs to switch processes */
    proc_timer_tick(regs);
}
