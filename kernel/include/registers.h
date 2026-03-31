#ifndef SIMPLE_REGISTERS_H
#define SIMPLE_REGISTERS_H

#include "types.h"

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

#endif
