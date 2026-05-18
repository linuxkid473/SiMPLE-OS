#ifndef SIMPLE_MOUSE_H
#define SIMPLE_MOUSE_H

#include "types.h"

/*
 * PS/2 mouse driver — framebuffer edition.
 *
 * Byte routing: keyboard.c reads every byte from port 0x60; when
 * status bit 5 (0x20) is set the byte belongs to the mouse.  That
 * byte is passed here.  No IRQ12 required — polling is enough for
 * a single-threaded kernel that already spins in the keyboard loop.
 *
 * Coordinate system: pixel position on the framebuffer, clamped to
 * [0, screen_w-1] × [0, screen_h-1].  Y increases downward (screen
 * convention); the PS/2 Y axis is inverted on arrival.
 */

/* Tell the driver the screen dimensions and reset the cursor to centre.
 * Must be called before mouse_init() whenever a framebuffer is available. */
void mouse_set_screen(int w, int h);

/* Initialise the PS/2 mouse device and enable data-reporting mode. */
void mouse_init(void);

/* Feed one byte from the PS/2 data port.  Called by keyboard.c. */
void mouse_handle_byte(uint8_t data);

/* Current cursor pixel position. */
int     mouse_get_x(void);
int     mouse_get_y(void);

/* Bitmask: bit 0 = left button, bit 1 = right button, bit 2 = middle. */
uint8_t mouse_get_buttons(void);

#endif
