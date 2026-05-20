#ifndef SIMPLE_PANIC_H
#define SIMPLE_PANIC_H

#include "types.h"
#include "registers.h"

void kernel_panic(const char* msg);
void kernel_panic_regs(const char* msg, uint32_t int_no, uint32_t err_code,
                       uint32_t eip, uint32_t cs, uint32_t eflags);
/* Full register dump — prefer this for exception handling */
void kernel_panic_full(const char* msg, registers_t* regs);

#endif
