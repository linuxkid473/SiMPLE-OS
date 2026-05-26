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

#endif
