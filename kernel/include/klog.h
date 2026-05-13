#ifndef SIMPLE_KLOG_H
#define SIMPLE_KLOG_H

#include "types.h"

void klog_init(void);
void klog(const char* subsystem, const char* msg);
void klog_hex(const char* subsystem, const char* label, uint32_t value);
void klog_dec(const char* subsystem, const char* label, uint32_t value);
void klog_boot(const char* milestone);
void klog_fail(const char* subsystem, const char* msg);

#endif
