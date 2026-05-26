#ifndef USER_STDIO_H
#define USER_STDIO_H

typedef struct _FILE FILE;

typedef __builtin_va_list va_list;
#define va_start(v,l)  __builtin_va_start(v,l)
#define va_arg(v,t)    __builtin_va_arg(v,t)
#define va_end(v)      __builtin_va_end(v)

typedef unsigned int size_t;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define EOF (-1)

FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *f);
int   fflush(FILE *f);

int   fputc(int c, FILE *f);
int   putchar(int c);
int   putc(int c, FILE *f);
int   fgetc(FILE *f);
int   getchar(void);
int   getc(FILE *f);
int   ungetc(int c, FILE *f);

int   feof(FILE *f);
int   ferror(FILE *f);
void  clearerr(FILE *f);

int   fputs(const char *s, FILE *f);
int   puts(const char *s);
char *fgets(char *buf, int size, FILE *f);

int   fwrite(const void *ptr, int size, int count, FILE *f);
int   fread(void *ptr, int size, int count, FILE *f);

int   fseek(FILE *f, long offset, int whence);
long  ftell(FILE *f);
void  rewind(FILE *f);
int   fileno(FILE *f);

int   vsnprintf(char *buf, int size, const char *fmt, va_list ap);
int   snprintf(char *buf, int size, const char *fmt, ...);
int   sprintf(char *buf, const char *fmt, ...);
int   vsprintf(char *buf, const char *fmt, va_list ap);

int   vfprintf(FILE *f, const char *fmt, va_list ap);
int   fprintf(FILE *f, const char *fmt, ...);
int   printf(const char *fmt, ...);
int   vprintf(const char *fmt, va_list ap);

#endif /* USER_STDIO_H */
