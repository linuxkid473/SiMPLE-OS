#ifndef SIMPLE_STRING_H
#define SIMPLE_STRING_H

#include "types.h"

size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t n);
void* memset(void* dst, int value, size_t count);
void* memcpy(void* dst, const void* src, size_t count);
int memcmp(const void* a, const void* b, size_t count);
char* strchr(const char* s, int ch);

void to_upper_str(char* s);
int starts_with(const char* s, const char* prefix);

#endif
