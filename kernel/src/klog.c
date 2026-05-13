#include "klog.h"
#include "serial.h"
#include "vga.h"

#define KLOG_PORT COM1

void klog_init(void) {
    serial_init(KLOG_PORT);
    serial_write(KLOG_PORT, "[SIMPLE] serial logging initialized\n");
}

void klog(const char* subsystem, const char* msg) {
    serial_write(KLOG_PORT, "[SIMPLE] ");
    if (subsystem) {
        serial_write(KLOG_PORT, subsystem);
        serial_write(KLOG_PORT, ": ");
    }
    if (msg) {
        serial_write(KLOG_PORT, msg);
    }
    serial_write(KLOG_PORT, "\n");
}

void klog_hex(const char* subsystem, const char* label, uint32_t value) {
    serial_write(KLOG_PORT, "[SIMPLE] ");
    if (subsystem) {
        serial_write(KLOG_PORT, subsystem);
        serial_write(KLOG_PORT, ": ");
    }
    if (label) {
        serial_write(KLOG_PORT, label);
        serial_write(KLOG_PORT, "=");
    }
    serial_write_hex(KLOG_PORT, value);
    serial_write(KLOG_PORT, "\n");
}

void klog_dec(const char* subsystem, const char* label, uint32_t value) {
    serial_write(KLOG_PORT, "[SIMPLE] ");
    if (subsystem) {
        serial_write(KLOG_PORT, subsystem);
        serial_write(KLOG_PORT, ": ");
    }
    if (label) {
        serial_write(KLOG_PORT, label);
        serial_write(KLOG_PORT, "=");
    }
    serial_write_dec(KLOG_PORT, value);
    serial_write(KLOG_PORT, "\n");
}

void klog_boot(const char* milestone) {
    serial_write(KLOG_PORT, "[SIMPLE] ");
    if (milestone) {
        serial_write(KLOG_PORT, milestone);
    }
    serial_write(KLOG_PORT, "\n");
}

void klog_fail(const char* subsystem, const char* msg) {
    serial_write(KLOG_PORT, "[SIMPLE] FAIL ");
    if (subsystem) {
        serial_write(KLOG_PORT, subsystem);
        serial_write(KLOG_PORT, ": ");
    }
    if (msg) {
        serial_write(KLOG_PORT, msg);
    }
    serial_write(KLOG_PORT, "\n");

    vga_write("[FAIL] ");
    if (subsystem) {
        vga_write(subsystem);
        vga_write(": ");
    }
    if (msg) {
        vga_write(msg);
    }
    vga_putc('\n');
}
