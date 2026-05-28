#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* Forward-declare the opaque FILE type (defined in user/stdio.c) */
struct _FILE;
typedef struct _FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF    (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Open / close */
FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *f);
int   fflush(FILE *f);

/* Character I/O */
int   fputc(int c, FILE *f);
int   putchar(int c);
int   putc(int c, FILE *f);
int   fgetc(FILE *f);
int   getchar(void);
int   getc(FILE *f);
int   ungetc(int c, FILE *f);

/* Line I/O */
int   fputs(const char *s, FILE *f);
int   puts(const char *s);
char *fgets(char *buf, int size, FILE *f);

/* Block I/O */
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *f);
size_t fread(void *ptr, size_t size, size_t count, FILE *f);

/* Seek / tell */
int   fseek(FILE *f, long offset, int whence);
long  ftell(FILE *f);
void  rewind(FILE *f);

/* Status */
int   feof(FILE *f);
int   ferror(FILE *f);
void  clearerr(FILE *f);
int   fileno(FILE *f);

/* Formatted output */
int   printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int   fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int   sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int   snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int   vprintf(const char *fmt, va_list ap);
int   vfprintf(FILE *f, const char *fmt, va_list ap);
int   vsprintf(char *buf, const char *fmt, va_list ap);
int   vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Error */
void  perror(const char *s);

/* File removal */
int   remove(const char *path);
int   rename(const char *old, const char *newp);

#endif
