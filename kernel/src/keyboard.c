#include "keyboard.h"
#include "io.h"
#include "kapp.h"
#include "mouse.h"
#include "pic.h"
#include "pit.h"
#include "process.h"
#include "serial.h"
#include "wm.h"

static int shift_pressed    = 0;
static int alt_pressed      = 0;
static int ctrl_pressed     = 0;
static int extended_prefix  = 0;

/* ================================================================
 * Scancode ring buffer — filled by keyboard_irq_handler (IRQ1),
 * drained by keyboard_read_event().
 * ================================================================ */
#define KB_BUF_SIZE 64
static volatile uint8_t kb_buf [KB_BUF_SIZE];
static volatile uint8_t kb_head = 0;   /* write pointer (IRQ1 side) */
static volatile uint8_t kb_tail = 0;   /* read  pointer (poll side) */

static void kb_push(uint8_t sc) {
    uint8_t next = (uint8_t)((kb_head + 1u) % KB_BUF_SIZE);
    if (next != kb_tail) {   /* drop if full */
        kb_buf[kb_head] = sc;
        kb_head = next;
    }
}

static int kb_pop(uint8_t *sc) {
    if (kb_tail == kb_head) return 0;
    *sc = kb_buf[kb_tail];
    kb_tail = (uint8_t)((kb_tail + 1u) % KB_BUF_SIZE);
    return 1;
}

int kb_scancode_available(void) {
    return kb_tail != kb_head;
}

void keyboard_inject_scancode(uint8_t sc) {
    kb_push(sc);
    if (sc != 0xE0u)
        wm_push_key(sc);
    proc_wake_kbd_waiters();
}

/* ================================================================
 * keyboard_irq_handler — IRQ1 (vector 0x21), called from isr_handler.
 *
 * Reads the byte from the PS/2 output buffer.
 * - Mouse bytes  (status bit 5 set): forwarded to mouse_handle_byte.
 * - Keyboard bytes: pushed to kb_buf AND injected into the WM slot
 *   queue via wm_push_key so GUI apps receive key events even while
 *   another process is sleeping.
 * Wakes any process blocked in proc_block_on_kbd().
 * Sends EOI before returning.
 * ================================================================ */
void keyboard_irq_handler(void) {
    uint8_t status = inb(0x64);
    if (status & 0x01u) {
        uint8_t data = inb(0x60);
        if (status & 0x20u) {
            mouse_handle_byte(data);
        } else {
            kb_push(data);
            /* Route to WM event system (non-blocking, just queue push) */
            if (data != 0xE0u) {
                wm_push_key(data);
                /* Phase 5: log key routing so we can see which slot owns input */
                if (!(data & 0x80u)) {  /* key-down only, reduce noise */
                    serial_write(COM1, "[INPUT] sc=0x");
                    serial_write_hex(COM1, data);
                    serial_write(COM1, " current_proc=");
                    if (current_proc >= 0) {
                        serial_write_dec(COM1, (uint32_t)current_proc);
                        serial_write(COM1, " pid=");
                        serial_write_dec(COM1, (uint32_t)proc_table[current_proc].pid);
                    } else {
                        serial_write(COM1, "ring0");
                    }
                    serial_write(COM1, "\n");
                }
            }
            /* Wake any process blocked waiting for keyboard stdin */
            proc_wake_kbd_waiters();
        }
    }
    pic_eoi(1);
}

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
    kb_head = 0;
    kb_tail = 0;

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

    /*
     * Enable keyboard interrupt (IRQ1) in the PS/2 controller command byte.
     * Without setting bit 0 of the command byte, the PS/2 controller never
     * asserts IRQ1 — unmasking it in the PIC alone is not sufficient.
     *
     * Command sequence:
     *   0x20 → read current command byte from controller
     *   0x60 → write new command byte to controller
     */
    for (uint32_t i = 0; i < 100000; i++)
        if ((inb(0x64) & 0x02) == 0) break;
    outb(0x64, 0x20);   /* Read Command Byte */
    for (uint32_t i = 0; i < 100000; i++)
        if (inb(0x64) & 0x01) break;
    uint8_t cb = inb(0x60);
    cb |= 0x01u;        /* bit 0: enable keyboard interrupt (IRQ1) */
    cb &= (uint8_t)~0x10u;  /* bit 4 clear: enable keyboard clock */
    for (uint32_t i = 0; i < 100000; i++)
        if ((inb(0x64) & 0x02) == 0) break;
    outb(0x64, 0x60);   /* Write Command Byte */
    for (uint32_t i = 0; i < 100000; i++)
        if ((inb(0x64) & 0x02) == 0) break;
    outb(0x60, cb);

    /* Unmask IRQ1 in the PIC so keyboard interrupts reach the CPU. */
    pic_unmask(1);
}

int keyboard_is_alt_pressed(void) {
    return alt_pressed;
}

int keyboard_is_ctrl_pressed(void) {
    return ctrl_pressed;
}

/*
 * Run one scancode through the modifier/extended-prefix state machine.
 * Returns 1 if a key event was produced, 0 if the byte only changed
 * internal state (modifiers, key releases, unknown codes, 0xE0 prefix).
 *
 * Shared by the blocking reader (keyboard_read_event) and the
 * non-blocking reader (keyboard_poll_event) so both see a single
 * consistent shift/ctrl/alt state.
 */
static int keyboard_translate(uint8_t scancode, key_event_t *event) {
    if (scancode == 0xE0) {
        extended_prefix = 1;
        return 0;
    }

    if (extended_prefix) {
        extended_prefix = 0;
        uint8_t code = scancode & 0x7F;

        /* Right Alt press / release (extended 0x38) */
        if (code == 0x38) {
            alt_pressed = (scancode & 0x80) ? 0 : 1;
            return 0;
        }

        if (scancode & 0x80) return 0;   /* other extended key releases */

        if (code == 0x4B) { event->type = KEY_EVENT_LEFT;   return 1; }
        if (code == 0x4D) { event->type = KEY_EVENT_RIGHT;  return 1; }
        if (code == 0x48) { event->type = KEY_EVENT_UP;     return 1; }
        if (code == 0x50) { event->type = KEY_EVENT_DOWN;   return 1; }
        if (code == 0x53) { event->type = KEY_EVENT_DELETE; return 1; }
        if (code == 0x47) { event->type = KEY_EVENT_HOME;   return 1; }
        if (code == 0x4F) { event->type = KEY_EVENT_END;    return 1; }
        if (code == 0x49) { event->type = KEY_EVENT_PGUP;   return 1; }
        if (code == 0x51) { event->type = KEY_EVENT_PGDN;   return 1; }
        return 0;
    }

    /* Shift press / release */
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return 0; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; return 0; }

    /* Left Alt press / release (0x38 press, 0xB8 = 0x80|0x38 release) */
    if (scancode == 0x38) { alt_pressed = 1; return 0; }
    if (scancode == 0xB8) { alt_pressed = 0; return 0; }

    /* Left Ctrl press / release (0x1D press, 0x9D release) */
    if (scancode == 0x1D) { ctrl_pressed = 1; return 0; }
    if (scancode == 0x9D) { ctrl_pressed = 0; return 0; }

    if (scancode & 0x80) return 0;

    if (scancode == 0x1C) {
        event->type = KEY_EVENT_ENTER;
        return 1;
    }

    if (scancode == 0x0E) {
        event->type = KEY_EVENT_BACKSPACE;
        return 1;
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
            return 1;
        }
    }
    return 0;
}

void keyboard_read_event(key_event_t* event) {
    event->type = KEY_EVENT_NONE;
    event->ch = 0;

    while (1) {
        uint8_t scancode;

        /* Fast path: drain ring buffer filled by IRQ1 handler */
        if (kb_pop(&scancode)) {
            /* wm_push_key() already called by keyboard_irq_handler */
            goto process_scancode;
        }

        /* Fallback: poll PS/2 directly.
         * Mouse bytes: forward to mouse driver.
         * Keyboard bytes: process directly WITHOUT pushing to ring buffer
         *   (avoids doubles — byte is consumed here, IRQ1 won't fire for it). */
        {
            uint8_t st = inb(0x64);
            if (st & 0x01u) {
                uint8_t data = inb(0x60);
                if (st & 0x20u) {
                    mouse_handle_byte(data);
                    continue;
                }
                /* Keyboard byte read directly (IRQ1 missed it). Route to the
                 * focused USER window's slot queue so ring-3 apps like snake
                 * receive the event even when ring-0 briefly holds the CPU. */
                if (data != 0xE0u)
                    wm_push_key(data);
                scancode = data;
                goto process_scancode;
            }
        }

        /* Both ring buffer and PS/2 are empty — sleep until next interrupt */
        __asm__ volatile("sti; hlt; cli" ::: "memory");
        /* Idle redraw only when animated kapps are open (games, clock, etc.) */
        if (kapp_any_open()) {
            static uint32_t idle_frame = 0;
            uint32_t t = pit_ticks();
            if (t - idle_frame >= 4) { idle_frame = t; wm_draw_all(); }
        }
        continue;

process_scancode:
        if (scancode == 0xE0) {
            extended_prefix = 1;
            continue;
        }

        /* NOTE: wm_push_key() called by IRQ handler (fast path) or
         * by the polling fallback above.  Do NOT call it again here. */
        keyboard_translate(scancode, event);
        /* Return after each non-prefix scancode, whether or not it produced
         * an event (preserves historical behaviour: callers loop on
         * KEY_EVENT_NONE). */
        return;
    }
}

int keyboard_poll_event(key_event_t* event) {
    event->type = KEY_EVENT_NONE;
    event->ch = 0;

    while (1) {
        uint8_t scancode;

        if (!kb_pop(&scancode)) {
            /* Ring empty — try one direct PS/2 poll (IRQ may be masked or
             * the byte may have raced the handler). */
            uint8_t st = inb(0x64);
            if (!(st & 0x01u))
                return 0;              /* genuinely no input pending */
            uint8_t data = inb(0x60);
            if (st & 0x20u) {
                mouse_handle_byte(data);
                continue;
            }
            if (data != 0xE0u)
                wm_push_key(data);
            scancode = data;
        }

        if (keyboard_translate(scancode, event))
            return 1;
        /* State-only byte (modifier/release/prefix) — keep draining. */
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
