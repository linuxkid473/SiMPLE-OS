#include "string.h"

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return (int)((unsigned char)*a - (unsigned char)*b);
        }
        a++;
        b++;
    }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

int strncmp(const char* a, const char* b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (int)((unsigned char)a[i] - (unsigned char)b[i]);
        }
    }
    return 0;
}

char* strcpy(char* dst, const char* src) {
    size_t i = 0;
    while (src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i++] = '\0';
    }
    return dst;
}

void* memset(void* dst, int value, size_t count) {
    uint8_t* d = (uint8_t*)dst;
    size_t i;
    for (i = 0; i < count; i++) {
        d[i] = (uint8_t)value;
    }
    return dst;
}

void* memcpy(void* dst, const void* src, size_t count) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    size_t i;
    for (i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t count) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    size_t i;
    for (i = 0; i < count; i++) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

char* strchr(const char* s, int ch) {
    while (*s) {
        if (*s == (char)ch) {
            return (char*)s;
        }
        s++;
    }
    if (ch == 0) {
        return (char*)s;
    }
    return NULL;
}

static char to_upper_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

void to_upper_str(char* s) {
    while (*s) {
        *s = to_upper_char(*s);
        s++;
    }
}

int starts_with(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}
