#include "serial.h"
#include "io.h"

#define SERIAL_DATA(b)           (b)
#define SERIAL_INT_ENABLE(b)     ((b) + 1)
#define SERIAL_FIFO_CTRL(b)      ((b) + 2)
#define SERIAL_LINE_CTRL(b)      ((b) + 3)
#define SERIAL_MODEM_CTRL(b)     ((b) + 4)
#define SERIAL_LINE_STATUS(b)    ((b) + 5)

#define SERIAL_LSR_TX_EMPTY      0x20

static uint16_t active_port = COM1;

void serial_init(uint16_t port) {
    active_port = port;

    outb(SERIAL_INT_ENABLE(port), 0x00);
    outb(SERIAL_LINE_CTRL(port), 0x80);
    outb(SERIAL_DATA(port), 0x03);
    outb(SERIAL_INT_ENABLE(port), 0x00);
    outb(SERIAL_LINE_CTRL(port), 0x03);
    outb(SERIAL_FIFO_CTRL(port), 0xC7);
    outb(SERIAL_MODEM_CTRL(port), 0x0B);
}

static int serial_can_write(uint16_t port) {
    return inb(SERIAL_LINE_STATUS(port)) & SERIAL_LSR_TX_EMPTY;
}

void serial_putc(uint16_t port, char c) {
    while (!serial_can_write(port)) {
    }
    outb(SERIAL_DATA(port), (uint8_t)c);
}

void serial_write(uint16_t port, const char* str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') {
            serial_putc(port, '\r');
        }
        serial_putc(port, *str);
        str++;
    }
}

void serial_write_hex(uint16_t port, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    serial_write(port, "0x");
    for (int i = 7; i >= 0; i--) {
        serial_putc(port, hex[(value >> (i * 4)) & 0xF]);
    }
}

void serial_write_dec(uint16_t port, uint32_t value) {
    if (value == 0) {
        serial_putc(port, '0');
        return;
    }
    char buf[12];
    int i = 0;
    while (value > 0 && i < 11) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        serial_putc(port, buf[--i]);
    }
}
