#ifndef SIMPLE_KEYBOARD_H
#define SIMPLE_KEYBOARD_H

#include "types.h"

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_CHAR,
    KEY_EVENT_ENTER,
    KEY_EVENT_BACKSPACE,
    KEY_EVENT_DELETE,
    KEY_EVENT_LEFT,
    KEY_EVENT_RIGHT,
    KEY_EVENT_UP,
    KEY_EVENT_DOWN
} key_event_type_t;

typedef struct {
    key_event_type_t type;
    char ch;
} key_event_t;

void keyboard_init(void);
void keyboard_read_event(key_event_t* event);
char keyboard_getchar(void);

/* Returns 1 if either Alt key is currently held, 0 otherwise. */
int keyboard_is_alt_pressed(void);

/* Returns 1 if Ctrl key is currently held, 0 otherwise. */
int keyboard_is_ctrl_pressed(void);

/* IRQ1 handler — called by isr_handler when int_no == 0x21.
 * Reads one PS/2 byte, pushes it to the scancode ring buffer,
 * routes it to the active WM window, and wakes any process
 * blocked waiting for keyboard input. */
void keyboard_irq_handler(void);

/* Returns 1 if the scancode ring buffer has at least one byte. */
int kb_scancode_available(void);

#endif
