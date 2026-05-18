#include "mouse.h"
#include "io.h"
#include "wm.h"

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */

static int     scr_w       = 800;   /* framebuffer width  (pixels) */
static int     scr_h       = 600;   /* framebuffer height (pixels) */

static int     mx          = 400;   /* current cursor X             */
static int     my          = 300;   /* current cursor Y             */
static uint8_t buttons     = 0;     /* current button bitmask       */
static uint8_t prev_btns   = 0;     /* buttons from previous packet */

static int     mouse_ready = 0;     /* set to 1 after successful init */
static uint8_t pkt[3];             /* assembling a 3-byte PS/2 packet  */
static int     pkt_idx     = 0;

/* ------------------------------------------------------------------ */
/* PS/2 controller helpers                                              */
/* ------------------------------------------------------------------ */

/* Wait until the PS/2 controller is ready to accept a byte (input buf empty). */
static int wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++)
        if ((inb(0x64) & 0x02) == 0) return 1;
    return 0;
}

/* Wait until the PS/2 output buffer has data (readable). */
static int wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++)
        if (inb(0x64) & 0x01) return 1;
    return 0;
}

/*
 * Send a command byte to the mouse device.
 * Route: write 0xD4 to port 0x64 (tells controller: next byte goes to mouse),
 *        then write the command to port 0x60.
 * Reads and discards the ACK byte (0xFA) the mouse sends back.
 */
static int mouse_send(uint8_t cmd) {
    if (!wait_write()) return 0;
    outb(0x64, 0xD4);
    if (!wait_write()) return 0;
    outb(0x60, cmd);
    if (!wait_read())  return 0;
    (void)inb(0x60);   /* discard ACK */
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void mouse_set_screen(int w, int h) {
    scr_w = w;
    scr_h = h;
    mx    = w / 2;
    my    = h / 2;
}

int     mouse_get_x(void)       { return mx;      }
int     mouse_get_y(void)       { return my;      }
uint8_t mouse_get_buttons(void) { return buttons; }

void mouse_init(void) {
    pkt_idx     = 0;
    mouse_ready = 0;
    buttons     = 0;
    prev_btns   = 0;

    /* 1. Enable the auxiliary (mouse) PS/2 port */
    if (!wait_write()) return;
    outb(0x64, 0xA8);

    /*
     * 2. Read the Controller Command Byte (CCB), then write it back with:
     *    bit 1 set   — enable IRQ12 (mouse interrupt enable)
     *    bit 5 clear — un-disable the mouse port
     * Note: we are polling, not using IRQ12, but clearing bit 5 is
     * required to allow the mouse to send data at all.
     */
    if (!wait_write()) return;
    outb(0x64, 0x20);          /* request CCB */
    if (!wait_read())  return;
    uint8_t ccb = inb(0x60);
    ccb |=  0x02;              /* enable IRQ12 in CCB */
    ccb &= ~0x20;              /* clear "mouse disabled" bit */
    if (!wait_write()) return;
    outb(0x64, 0x60);          /* write CCB */
    if (!wait_write()) return;
    outb(0x60, ccb);

    /* 3. Tell the mouse to use default settings (rate 100, res 4, scaling 1:1) */
    if (!mouse_send(0xF6)) return;

    /* 4. Enable streaming (mouse starts sending packets on movement) */
    if (!mouse_send(0xF4)) return;

    mouse_ready = 1;
}

/* ------------------------------------------------------------------ */
/* Packet handler                                                       */
/* ------------------------------------------------------------------ */

/*
 * PS/2 standard 3-byte mouse packet layout:
 *
 *   Byte 0:  YOV XOV YS XS  1  MB RB LB
 *   Byte 1:  X movement (low 8 bits; sign = byte0.XS)
 *   Byte 2:  Y movement (low 8 bits; sign = byte0.YS)
 *
 *   Bits YOV/XOV: overflow flags — discard packet if either is set.
 *   Bit  3  (always 1): used to re-sync packet boundaries.
 *   LB/RB/MB: left / right / middle button states.
 *
 * Y axis: positive PS/2 = move up = decrease screen Y.
 * We negate dy so moving the physical mouse up moves the cursor up.
 */
void mouse_handle_byte(uint8_t data) {
    if (!mouse_ready) return;

    /*
     * Sync: the first byte of every packet always has bit 3 set.
     * If we are waiting for byte 0 and bit 3 is clear, this byte
     * is a stale leftover from a partial packet — drop it.
     */
    if (pkt_idx == 0 && !(data & 0x08)) return;

    pkt[pkt_idx++] = data;
    if (pkt_idx < 3) return;   /* need all three bytes before processing */
    pkt_idx = 0;

    /* Discard packets where X or Y overflowed (values would be garbage) */
    if (pkt[0] & 0xC0) return;

    /* Extract buttons and movement deltas */
    buttons = pkt[0] & 0x07;

    /*
     * Sign-extend the 8-bit movement bytes.  The PS/2 spec describes
     * 9-bit signed values (sign bit in byte 0 bits 4/5) but casting to
     * int8_t gives correct sign extension for movements that fit in
     * ±127 pixels — which covers all normal QEMU mouse speeds.
     */
    int dx =  (int)(int8_t)pkt[1];
    int dy = -(int)(int8_t)pkt[2];   /* invert: PS/2 up = positive screen Y */

    mx += dx;
    my += dy;

    /* Clamp to screen bounds */
    if (mx < 0)      mx = 0;
    if (mx >= scr_w) mx = scr_w - 1;
    if (my < 0)      my = 0;
    if (my >= scr_h) my = scr_h - 1;

    /*
     * Hand off to the window manager.
     * wm_handle_mouse() performs focus, drag, and calls wm_draw_all(),
     * which draws the cursor as its very last step using mouse_get_x/y.
     */
    wm_handle_mouse(mx, my, buttons, prev_btns);
    prev_btns = buttons;
}
