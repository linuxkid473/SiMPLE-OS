#ifndef SIMPLE_USB_HID_H
#define SIMPLE_USB_HID_H

#include "types.h"
#include "ehci.h"

/*
 * Process a completed USB HID keyboard boot-protocol report.
 * report[0] = modifier byte, report[2..7] = keycodes.
 * Compares with prev_report to generate make/break scancodes.
 */
void usb_hid_kbd_report(const uint8_t *report, uint8_t *prev_report);

/*
 * Process a completed USB HID mouse boot-protocol report.
 * report[0] = buttons, report[1] = X rel (signed), report[2] = Y rel (signed).
 */
void usb_hid_mouse_report(const uint8_t *report);

#endif
