#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

/* Memory allocation */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);

/* Process control */
void  exit(int code) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));

/* Environment */
char *getenv(const char *name);
int   setenv(const char *name, const char *value, int overwrite);
int   unsetenv(const char *name);
int   putenv(char *string);

/* String conversion */
int          atoi(const char *s);
long         atol(const char *s);
long         strtol(const char *s, char **endp, int base);
unsigned long strtoul(const char *s, char **endp, int base);
double       atof(const char *s);

/* Misc */
int   abs(int x);
long  labs(long x);

/* Sorting / searching */
void  qsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif
