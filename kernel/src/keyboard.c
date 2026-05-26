#include "keyboard.h"
#include "io.h"
#include "mouse.h"
#include "wm.h"

static int shift_pressed    = 0;
static int alt_pressed      = 0;
static int ctrl_pressed     = 0;
static int extended_prefix  = 0;

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
    shift_pressed   = 0;
    alt_pressed     = 0;
    ctrl_pressed    = 0;
    extended_prefix = 0;

    /* Re-enable the keyboard port (command 0xAE to the PS/2 controller).
     * Limine and some firmware leave the port disabled before handoff. */
    for (uint32_t i = 0; i < 100000; i++)
        if ((inb(0x64) & 0x02) == 0) break;
    outb(0x64, 0xAE);

    /* Flush any stale bytes left in the output buffer by the bootloader. */
    for (uint32_t i = 0; i < 32; i++) {
        if ((inb(0x64) & 0x01) == 0) break;
        (void)inb(0x60);
    }
}

int keyboard_is_alt_pressed(void) {
    return alt_pressed;
}

int keyboard_is_ctrl_pressed(void) {
    return ctrl_pressed;
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

        /* Inject raw scancode into the user WM event queue.
         * Extended-key prefixes (0xE0) are filtered above; all other
         * scancodes — presses (bit7=0) and releases (bit7=1) — are
         * forwarded so user programs get the full PS/2 stream. */
        wm_push_key(scancode);

        if (extended_prefix) {
            extended_prefix = 0;
            uint8_t code = scancode & 0x7F;

            /* Right Alt press / release (extended 0x38) */
            if (code == 0x38) {
                alt_pressed = (scancode & 0x80) ? 0 : 1;
                return;
            }

            if (scancode & 0x80) return;   /* other extended key releases */

            if (code == 0x4B) { event->type = KEY_EVENT_LEFT;   return; }
            if (code == 0x4D) { event->type = KEY_EVENT_RIGHT;  return; }
            if (code == 0x48) { event->type = KEY_EVENT_UP;     return; }
            if (code == 0x50) { event->type = KEY_EVENT_DOWN;   return; }
            if (code == 0x53) { event->type = KEY_EVENT_DELETE; return; }
            return;
        }

        /* Left Shift press / release */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = 1;
            return;
        }
        if (scancode == 0xAA || scancode == 0xB6) {
            shift_pressed = 0;
            return;
        }

        /* Left Alt press / release (0x38 press, 0xB8 = 0x80|0x38 release) */
        if (scancode == 0x38) { alt_pressed = 1; return; }
        if (scancode == 0xB8) { alt_pressed = 0; return; }

        /* Left Ctrl press / release (0x1D press, 0x9D release) */
        if (scancode == 0x1D) { ctrl_pressed = 1; return; }
        if (scancode == 0x9D) { ctrl_pressed = 0; return; }

        if (scancode & 0x80) return;

        if (scancode == 0x1C) {
            event->type = KEY_EVENT_ENTER;
            return;
        }

        if (scancode == 0x0E) {
            event->type = KEY_EVENT_BACKSPACE;
            return;
        }

        if (scancode < 128) {
            char c = shift_pressed ? shift_keymap[scancode] : keymap[scancode];
            if (c) {
                /* If Ctrl is pressed and it's a letter, convert to control character */
                if (ctrl_pressed && c >= 'a' && c <= 'z') {
                    c = (char)(c - 'a' + 1);
                } else if (ctrl_pressed && c >= 'A' && c <= 'Z') {
                    c = (char)(c - 'A' + 1);
                }
                event->type = KEY_EVENT_CHAR;
                event->ch = c;
                return;
            }
        }
        return;
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
