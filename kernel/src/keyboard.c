#include "keyboard.h"
#include "io.h"
#include "mouse.h"

static int shift_pressed = 0;
static int extended_prefix = 0;

static const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*',
    0, ' ', 0
};

static const char shift_keymap[128] = {
    0, 27, '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*',
    0, ' ', 0
};

void keyboard_init(void) {
    shift_pressed = 0;
    extended_prefix = 0;
}

void keyboard_read_event(key_event_t* event) {
    event->type = KEY_EVENT_NONE;
    event->ch = 0;

    while (1) {
        uint8_t status = inb(0x64);
        if ((status & 0x01) == 0) {
            continue;
        }

        uint8_t scancode = inb(0x60);
        if (status & 0x20) {
            mouse_handle_byte(scancode);
            continue;
        }

        if (scancode == 0xE0) {
            extended_prefix = 1;
            continue;
        }

        int released = (scancode & 0x80) != 0;
        uint8_t code = (uint8_t)(scancode & 0x7F);

        if (extended_prefix) {
            extended_prefix = 0;
            if (released) {
                continue;
            }

            if (code == 0x4B) {
                event->type = KEY_EVENT_LEFT;
                return;
            }
            if (code == 0x4D) {
                event->type = KEY_EVENT_RIGHT;
                return;
            }
            if (code == 0x48) {
                event->type = KEY_EVENT_UP;
                return;
            }
            if (code == 0x50) {
                event->type = KEY_EVENT_DOWN;
                return;
            }
            if (code == 0x53) {
                event->type = KEY_EVENT_DELETE;
                return;
            }
            continue;
        }

        if (code == 0x2A || code == 0x36) {
            shift_pressed = released ? 0 : 1;
            continue;
        }

        if (released) {
            continue;
        }

        if (code == 0x1C) {
            event->type = KEY_EVENT_ENTER;
            return;
        }

        if (code == 0x0E) {
            event->type = KEY_EVENT_BACKSPACE;
            return;
        }

        char c = shift_pressed ? shift_keymap[code] : keymap[code];
        if (c) {
            event->type = KEY_EVENT_CHAR;
            event->ch = c;
            return;
        }
    }
}

char keyboard_getchar(void) {
    key_event_t event;
    while (1) {
        keyboard_read_event(&event);
        if (event.type == KEY_EVENT_CHAR) {
            return event.ch;
        }
        if (event.type == KEY_EVENT_ENTER) {
            return '\n';
        }
        if (event.type == KEY_EVENT_BACKSPACE) {
            return '\b';
        }
    }
}
