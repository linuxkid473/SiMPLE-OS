#ifndef SIMPLE_SERIAL_H
#define SIMPLE_SERIAL_H

#include "types.h"

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

void serial_init(uint16_t port);
void serial_putc(uint16_t port, char c);
void serial_write(uint16_t port, const char* str);
void serial_write_hex(uint16_t port, uint32_t value);
void serial_write_dec(uint16_t port, uint32_t value);

#endif
