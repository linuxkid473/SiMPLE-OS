#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

/* Memory */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memchr(const void *s, int c, size_t n);

/* String */
size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
char  *strstr(const char *haystack, const char *needle);
char  *strdup(const char *s);
char  *strtok(char *str, const char *delim);
char  *strtok_r(char *str, const char *delim, char **saveptr);

/* Conversion */
long   strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);

#endif
