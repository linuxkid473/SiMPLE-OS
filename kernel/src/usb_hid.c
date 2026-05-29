#include "usb_hid.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"

/*
 * HID Usage ID → PS/2 Set 1 make scancode translation.
 * Values ≥ 0x100 require an E0 prefix before the base code (bits 7:0).
 * 0x000 = no mapping (key ignored).
 */
static const uint16_t g_hid2ps2[256] = {
    /* 0x00 */ 0,         /* No key */
    /* 0x01 */ 0,         /* ErrorRollOver */
    /* 0x02 */ 0,         /* POSTFail */
    /* 0x03 */ 0,         /* ErrorUndefined */
    /* 0x04 */ 0x1E,      /* A */
    /* 0x05 */ 0x30,      /* B */
    /* 0x06 */ 0x2E,      /* C */
    /* 0x07 */ 0x20,      /* D */
    /* 0x08 */ 0x12,      /* E */
    /* 0x09 */ 0x21,      /* F */
    /* 0x0A */ 0x22,      /* G */
    /* 0x0B */ 0x23,      /* H */
    /* 0x0C */ 0x17,      /* I */
    /* 0x0D */ 0x24,      /* J */
    /* 0x0E */ 0x25,      /* K */
    /* 0x0F */ 0x26,      /* L */
    /* 0x10 */ 0x32,      /* M */
    /* 0x11 */ 0x31,      /* N */
    /* 0x12 */ 0x18,      /* O */
    /* 0x13 */ 0x19,      /* P */
    /* 0x14 */ 0x10,      /* Q */
    /* 0x15 */ 0x13,      /* R */
    /* 0x16 */ 0x1F,      /* S */
    /* 0x17 */ 0x14,      /* T */
    /* 0x18 */ 0x16,      /* U */
    /* 0x19 */ 0x2F,      /* V */
    /* 0x1A */ 0x11,      /* W */
    /* 0x1B */ 0x2D,      /* X */
    /* 0x1C */ 0x15,      /* Y */
    /* 0x1D */ 0x2C,      /* Z */
    /* 0x1E */ 0x02,      /* 1 */
    /* 0x1F */ 0x03,      /* 2 */
    /* 0x20 */ 0x04,      /* 3 */
    /* 0x21 */ 0x05,      /* 4 */
    /* 0x22 */ 0x06,      /* 5 */
    /* 0x23 */ 0x07,      /* 6 */
    /* 0x24 */ 0x08,      /* 7 */
    /* 0x25 */ 0x09,      /* 8 */
    /* 0x26 */ 0x0A,      /* 9 */
    /* 0x27 */ 0x0B,      /* 0 */
    /* 0x28 */ 0x1C,      /* Enter */
    /* 0x29 */ 0x01,      /* Escape */
    /* 0x2A */ 0x0E,      /* Backspace */
    /* 0x2B */ 0x0F,      /* Tab */
    /* 0x2C */ 0x39,      /* Space */
    /* 0x2D */ 0x0C,      /* - */
    /* 0x2E */ 0x0D,      /* = */
    /* 0x2F */ 0x1A,      /* [ */
    /* 0x30 */ 0x1B,      /* ] */
    /* 0x31 */ 0x2B,      /* \ */
    /* 0x32 */ 0x2B,      /* Non-US # */
    /* 0x33 */ 0x27,      /* ; */
    /* 0x34 */ 0x28,      /* ' */
    /* 0x35 */ 0x29,      /* ` */
    /* 0x36 */ 0x33,      /* , */
    /* 0x37 */ 0x34,      /* . */
    /* 0x38 */ 0x35,      /* / */
    /* 0x39 */ 0x3A,      /* Caps Lock */
    /* 0x3A */ 0x3B,      /* F1 */
    /* 0x3B */ 0x3C,      /* F2 */
    /* 0x3C */ 0x3D,      /* F3 */
    /* 0x3D */ 0x3E,      /* F4 */
    /* 0x3E */ 0x3F,      /* F5 */
    /* 0x3F */ 0x40,      /* F6 */
    /* 0x40 */ 0x41,      /* F7 */
    /* 0x41 */ 0x42,      /* F8 */
    /* 0x42 */ 0x43,      /* F9 */
    /* 0x43 */ 0x44,      /* F10 */
    /* 0x44 */ 0x57,      /* F11 */
    /* 0x45 */ 0x58,      /* F12 */
    /* 0x46 */ 0x100|0x37,/* Print Screen */
    /* 0x47 */ 0x46,      /* Scroll Lock */
    /* 0x48 */ 0x45,      /* Pause/Break */
    /* 0x49 */ 0x100|0x52,/* Insert */
    /* 0x4A */ 0x100|0x47,/* Home */
    /* 0x4B */ 0x100|0x49,/* Page Up */
    /* 0x4C */ 0x100|0x53,/* Delete */
    /* 0x4D */ 0x100|0x4F,/* End */
    /* 0x4E */ 0x100|0x51,/* Page Down */
    /* 0x4F */ 0x100|0x4D,/* Right Arrow */
    /* 0x50 */ 0x100|0x4B,/* Left Arrow */
    /* 0x51 */ 0x100|0x50,/* Down Arrow */
    /* 0x52 */ 0x100|0x48,/* Up Arrow */
    /* 0x53 */ 0x45,      /* Num Lock */
    /* 0x54 */ 0x100|0x35,/* Keypad / */
    /* 0x55 */ 0x37,      /* Keypad * */
    /* 0x56 */ 0x4A,      /* Keypad - */
    /* 0x57 */ 0x4E,      /* Keypad + */
    /* 0x58 */ 0x100|0x1C,/* Keypad Enter */
    /* 0x59 */ 0x4F,      /* Keypad 1 */
    /* 0x5A */ 0x50,      /* Keypad 2 */
    /* 0x5B */ 0x51,      /* Keypad 3 */
    /* 0x5C */ 0x4B,      /* Keypad 4 */
    /* 0x5D */ 0x4C,      /* Keypad 5 */
    /* 0x5E */ 0x4D,      /* Keypad 6 */
    /* 0x5F */ 0x47,      /* Keypad 7 */
    /* 0x60 */ 0x48,      /* Keypad 8 */
    /* 0x61 */ 0x49,      /* Keypad 9 */
    /* 0x62 */ 0x52,      /* Keypad 0 */
    /* 0x63 */ 0x53,      /* Keypad . */
    /* 0x64 */ 0x56,      /* Non-US \ */
    /* 0x65 */ 0x100|0x5F,/* Application/Menu */
    /* 0x66-0xDF */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                    0,0,0,0,0,0,0,0,0,0,
    /* 0xE0 */ 0x1D,       /* Left Ctrl */
    /* 0xE1 */ 0x2A,       /* Left Shift */
    /* 0xE2 */ 0x38,       /* Left Alt */
    /* 0xE3 */ 0x100|0x5B, /* Left GUI */
    /* 0xE4 */ 0x100|0x1D, /* Right Ctrl */
    /* 0xE5 */ 0x36,       /* Right Shift */
    /* 0xE6 */ 0x100|0x38, /* Right Alt */
    /* 0xE7 */ 0x100|0x5C, /* Right GUI */
    /* 0xE8-0xFF */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* PS/2 Set 1 scancodes for modifier key bits [0..7] in HID byte 0 */
static const uint16_t g_mod_ps2[8] = {
    0x1D,        /* Left Ctrl */
    0x2A,        /* Left Shift */
    0x38,        /* Left Alt */
    0x100|0x5B,  /* Left GUI */
    0x100|0x1D,  /* Right Ctrl */
    0x36,        /* Right Shift */
    0x100|0x38,  /* Right Alt */
    0x100|0x5C,  /* Right GUI */
};

/* Inject a PS/2 Set 1 make or break scancode (may need E0 prefix). */
static void inject(uint16_t ps2code, int press) {
    if (!ps2code) return;
    uint8_t base = (uint8_t)(ps2code & 0xFF);
    if (ps2code & 0x100) {
        keyboard_inject_scancode(0xE0);
        keyboard_inject_scancode(press ? base : (uint8_t)(base | 0x80));
    } else {
        keyboard_inject_scancode(press ? base : (uint8_t)(base | 0x80));
    }
}

void usb_hid_kbd_report(const uint8_t *report, uint8_t *prev)
{
    uint8_t mods     = report[0];
    uint8_t prev_mod = prev[0];

    /* Modifier changes */
    for (int b = 0; b < 8; b++) {
        int was = (prev_mod >> b) & 1;
        int now = (mods     >> b) & 1;
        if (was && !now) inject(g_mod_ps2[b], 0);  /* release */
        if (!was && now)  inject(g_mod_ps2[b], 1);  /* press   */
    }

    /* Key releases: keys in prev[2..7] not in report[2..7] */
    for (int i = 2; i < 8; i++) {
        uint8_t k = prev[i];
        if (!k || k == 0x01) continue;
        int still_held = 0;
        for (int j = 2; j < 8; j++) {
            if (report[j] == k) { still_held = 1; break; }
        }
        if (!still_held) inject(g_hid2ps2[k], 0);
    }

    /* Key presses: keys in report[2..7] not in prev[2..7] */
    for (int i = 2; i < 8; i++) {
        uint8_t k = report[i];
        if (!k || k == 0x01) continue;
        int was_held = 0;
        for (int j = 2; j < 8; j++) {
            if (prev[j] == k) { was_held = 1; break; }
        }
        if (!was_held) inject(g_hid2ps2[k], 1);
    }

    /* Update previous report */
    for (int i = 0; i < 8; i++) prev[i] = report[i];
}

void usb_hid_mouse_report(const uint8_t *report)
{
    uint8_t btns = report[0] & 0x07;
    int dx = (int)(int8_t)report[1];
    int dy = (int)(int8_t)report[2];
    mouse_inject_usb(dx, dy, btns);
}
