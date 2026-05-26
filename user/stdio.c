/*
 * user/stdio.c — freestanding stdio (FILE*, printf, sprintf, vsnprintf)
 *
 * Uses raw syscalls from libc.c: read/write/open/close/lseek.
 * Linked together with libc.c for any program that needs stdio.
 */

#include "stdio.h"

/* ---- internal syscall prototypes (provided by libc.c) ---- */
int read(int fd, void *buf, int len);
int write(int fd, const void *buf, int len);
int open(const char *path, int flags, ...);
int close(int fd);
int lseek(int fd, int offset, int whence);

/* ---- open flags (match fd.h) ---- */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200
#define O_APPEND  0x400

/* ---- string helpers (provided by libc.c) ---- */
static int _strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

/* ================================================================
 * FILE structure
 * ================================================================ */
#define FILE_BUF_SIZE  512
#define FILE_MAX       16

#define _IOFBF  0   /* fully buffered   */
#define _IOLBF  1   /* line buffered    */
#define _IONBF  2   /* unbuffered       */

struct _FILE {
    int  fd;
    int  mode;          /* O_RDONLY / O_WRONLY / O_RDWR */
    int  flags;
    int  eof;
    int  err;
    int  unget_valid;
    int  unget_char;

    /* write buffer */
    char wbuf[FILE_BUF_SIZE];
    int  wpos;

    /* read buffer */
    char rbuf[FILE_BUF_SIZE];
    int  rpos;
    int  rlen;
};

static struct _FILE _file_pool[FILE_MAX];
static int          _file_pool_init = 0;

static void _pool_init(void) {
    if (_file_pool_init) return;
    for (int i = 0; i < FILE_MAX; i++) {
        _file_pool[i].fd  = -1;
        _file_pool[i].err =  0;
        _file_pool[i].eof =  0;
    }
    /* Pre-wire stdin/stdout/stderr to fds 0/1/2 */
    _file_pool[0].fd = 0; _file_pool[0].mode = O_RDONLY;
    _file_pool[1].fd = 1; _file_pool[1].mode = O_WRONLY;
    _file_pool[2].fd = 2; _file_pool[2].mode = O_WRONLY;
    _file_pool_init = 1;
}

FILE *stdin  = &_file_pool[0];
FILE *stdout = &_file_pool[1];
FILE *stderr = &_file_pool[2];

static FILE *_alloc_file(void) {
    _pool_init();
    for (int i = 3; i < FILE_MAX; i++) {
        if (_file_pool[i].fd < 0) {
            _file_pool[i].eof =  0;
            _file_pool[i].err =  0;
            _file_pool[i].wpos = 0;
            _file_pool[i].rpos = 0;
            _file_pool[i].rlen = 0;
            _file_pool[i].unget_valid = 0;
            return &_file_pool[i];
        }
    }
    return (FILE *)0;
}

/* ================================================================
 * fopen / fclose
 * ================================================================ */
FILE *fopen(const char *path, const char *m) {
    _pool_init();
    int flags = 0;
    char c = m[0];
    if (c == 'r') {
        flags = O_RDONLY;
        if (m[1] == '+') flags = O_RDWR;
    } else if (c == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (m[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (c == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (m[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        return (FILE *)0;
    }

    int fd = open(path, flags, 0644);
    if (fd < 0) return (FILE *)0;

    FILE *f = _alloc_file();
    if (!f) { close(fd); return (FILE *)0; }
    f->fd   = fd;
    f->mode = flags & 3;
    f->wpos = 0;
    f->rpos = 0;
    f->rlen = 0;
    return f;
}

static int _flush_write(FILE *f) {
    if (f->wpos <= 0) return 0;
    int n = write(f->fd, f->wbuf, f->wpos);
    f->wpos = 0;
    return (n < 0) ? -1 : 0;
}

int fclose(FILE *f) {
    if (!f || f->fd < 0) return -1;
    _flush_write(f);
    int r = close(f->fd);
    f->fd = -1;
    return r;
}

int fflush(FILE *f) {
    if (!f) return 0;
    return _flush_write(f);
}

/* ================================================================
 * fputc / fgetc / ungetc
 * ================================================================ */
int fputc(int c, FILE *f) {
    if (!f || f->fd < 0) return -1;
    f->wbuf[f->wpos++] = (char)c;
    if (f->wpos >= FILE_BUF_SIZE || c == '\n') {
        if (_flush_write(f) < 0) { f->err = 1; return -1; }
    }
    return (unsigned char)c;
}

int putchar(int c) { return fputc(c, stdout); }
int putc(int c, FILE *f) { return fputc(c, f); }

static int _fill_rbuf(FILE *f) {
    f->rpos = 0;
    f->rlen = read(f->fd, f->rbuf, FILE_BUF_SIZE);
    if (f->rlen <= 0) { f->eof = 1; f->rlen = 0; return -1; }
    return 0;
}

int fgetc(FILE *f) {
    if (!f || f->fd < 0) return -1;
    if (f->unget_valid) { f->unget_valid = 0; return (unsigned char)f->unget_char; }
    if (f->rpos >= f->rlen) {
        if (_fill_rbuf(f) < 0) return -1;
    }
    return (unsigned char)f->rbuf[f->rpos++];
}

int getchar(void) { return fgetc(stdin); }
int getc(FILE *f) { return fgetc(f); }

int ungetc(int c, FILE *f) {
    if (!f || f->unget_valid) return -1;
    f->unget_char  = c;
    f->unget_valid = 1;
    f->eof = 0;
    return c;
}

int feof(FILE *f)   { return f ? f->eof : 1; }
int ferror(FILE *f) { return f ? f->err : 1; }
void clearerr(FILE *f) { if (f) { f->eof = 0; f->err = 0; } }

/* ================================================================
 * fputs / fgets / puts
 * ================================================================ */
int fputs(const char *s, FILE *f) {
    int n = _strlen(s);
    for (int i = 0; i < n; i++) {
        if (fputc((unsigned char)s[i], f) < 0) return -1;
    }
    return n;
}

int puts(const char *s) {
    int r = fputs(s, stdout);
    if (r < 0) return -1;
    fputc('\n', stdout);
    return r + 1;
}

char *fgets(char *buf, int size, FILE *f) {
    if (!buf || size <= 0 || !f) return (char *)0;
    int i;
    for (i = 0; i < size - 1; ) {
        int c = fgetc(f);
        if (c < 0) { if (i == 0) return (char *)0; break; }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

/* ================================================================
 * fread / fwrite
 * ================================================================ */
int fwrite(const void *ptr, int size, int count, FILE *f) {
    if (!f || !ptr || size <= 0 || count <= 0) return 0;
    const char *p = (const char *)ptr;
    int total = size * count;
    int done = 0;
    while (done < total) {
        int space = FILE_BUF_SIZE - f->wpos;
        int chunk = total - done;
        if (chunk > space) chunk = space;
        for (int i = 0; i < chunk; i++) f->wbuf[f->wpos++] = p[done + i];
        done += chunk;
        if (f->wpos >= FILE_BUF_SIZE) {
            if (_flush_write(f) < 0) break;
        }
    }
    return done / size;
}

int fread(void *ptr, int size, int count, FILE *f) {
    if (!f || !ptr || size <= 0 || count <= 0) return 0;
    char *p = (char *)ptr;
    int total = size * count;
    int done = 0;
    while (done < total) {
        int c = fgetc(f);
        if (c < 0) break;
        p[done++] = (char)c;
    }
    return done / size;
}

/* ================================================================
 * fseek / ftell / rewind
 * ================================================================ */
int fseek(FILE *f, long offset, int whence) {
    if (!f || f->fd < 0) return -1;
    _flush_write(f);
    f->rpos = 0; f->rlen = 0; f->eof = 0;
    return lseek(f->fd, (int)offset, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f) {
    if (!f || f->fd < 0) return -1;
    return (long)lseek(f->fd, 0, 1 /* SEEK_CUR */);
}

void rewind(FILE *f) { fseek(f, 0, 0 /* SEEK_SET */); }

int fileno(FILE *f) { return f ? f->fd : -1; }

/* ================================================================
 * vsnprintf — the core formatter
 * ================================================================ */
static void _out(char **dst, int *rem, char c) {
    if (*rem > 1) { **dst = c; (*dst)++; (*rem)--; }
}

static void _out_str(char **dst, int *rem, const char *s, int width, int left) {
    int len = _strlen(s);
    if (!left) {
        for (int i = len; i < width; i++) _out(dst, rem, ' ');
    }
    for (int i = 0; i < len; i++) _out(dst, rem, s[i]);
    if (left) {
        for (int i = len; i < width; i++) _out(dst, rem, ' ');
    }
}

static char _hex[] = "0123456789abcdef";
static char _HEX[] = "0123456789ABCDEF";

static void _fmt_uint(char **dst, int *rem, unsigned long v, int base,
                       int upper, int width, int zero_pad, int left,
                       int alt, int plus) {
    char tmp[32];
    int n = 0;
    const char *digits = upper ? _HEX : _hex;
    if (v == 0) { tmp[n++] = '0'; }
    else { while (v) { tmp[n++] = digits[v % (unsigned)base]; v /= (unsigned)base; } }

    /* prefix */
    int prefix_len = 0;
    char prefix[3] = {0};
    if (alt && base == 16 && v != 0) { prefix[0]='0'; prefix[1]=upper?'X':'x'; prefix_len=2; }
    else if (alt && base == 8) { prefix[0]='0'; prefix_len=1; }
    else if (plus) { prefix[0]='+'; prefix_len=1; }

    int num_width = n + prefix_len;
    char pad = zero_pad ? '0' : ' ';

    if (!left) {
        if (zero_pad) {
            for (int i = 0; i < prefix_len; i++) _out(dst, rem, prefix[i]);
            for (int i = num_width; i < width; i++) _out(dst, rem, '0');
        } else {
            for (int i = num_width; i < width; i++) _out(dst, rem, pad);
            for (int i = 0; i < prefix_len; i++) _out(dst, rem, prefix[i]);
        }
    } else {
        for (int i = 0; i < prefix_len; i++) _out(dst, rem, prefix[i]);
    }
    for (int i = n - 1; i >= 0; i--) _out(dst, rem, tmp[i]);
    if (left) for (int i = num_width; i < width; i++) _out(dst, rem, ' ');
}

static void _fmt_int(char **dst, int *rem, long v, int base,
                      int width, int zero_pad, int left, int plus) {
    char sign = 0;
    unsigned long uv;
    if (v < 0) { sign = '-'; uv = (unsigned long)(-v); }
    else        { sign = plus ? '+' : 0; uv = (unsigned long)v; }

    char tmp[32];
    int n = 0;
    if (uv == 0) tmp[n++] = '0';
    else { while (uv) { tmp[n++] = _hex[uv % 10]; uv /= 10; } }

    int num_width = n + (sign ? 1 : 0);
    char pad = zero_pad ? '0' : ' ';

    if (!left) {
        if (zero_pad) {
            if (sign) _out(dst, rem, sign);
            for (int i = num_width; i < width; i++) _out(dst, rem, '0');
        } else {
            for (int i = num_width; i < width; i++) _out(dst, rem, pad);
            if (sign) _out(dst, rem, sign);
        }
    } else {
        if (sign) _out(dst, rem, sign);
    }
    for (int i = n - 1; i >= 0; i--) _out(dst, rem, tmp[i]);
    if (left) for (int i = num_width; i < width; i++) _out(dst, rem, ' ');
}

typedef __builtin_va_list va_list;
#define va_start(v,l)  __builtin_va_start(v,l)
#define va_arg(v,t)    __builtin_va_arg(v,t)
#define va_end(v)      __builtin_va_end(v)

int vsnprintf(char *buf, int size, const char *fmt, va_list ap) {
    char *dst = buf;
    int rem = size;

    for (; *fmt; fmt++) {
        if (*fmt != '%') { _out(&dst, &rem, *fmt); continue; }
        fmt++;
        if (!*fmt) break;

        /* flags */
        int left=0, zero_pad=0, alt=0, plus=0, space=0;
        for (;;) {
            if      (*fmt == '-') { left     = 1; fmt++; }
            else if (*fmt == '0') { zero_pad = 1; fmt++; }
            else if (*fmt == '#') { alt      = 1; fmt++; }
            else if (*fmt == '+') { plus     = 1; fmt++; }
            else if (*fmt == ' ') { space    = 1; fmt++; }
            else break;
        }
        (void)space;

        /* width */
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt-'0'); fmt++; }

        /* precision (ignored for now) */
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') { va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') fmt++;
        }

        /* length modifier */
        int lng = 0;
        if (*fmt == 'l') { lng = 1; fmt++; if (*fmt == 'l') { lng=2; fmt++; } }
        else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }
        else if (*fmt == 'z' || *fmt == 't') { lng = 1; fmt++; }

        char spec = *fmt;
        switch (spec) {
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
            _fmt_int(&dst, &rem, v, 10, width, zero_pad, left, plus);
            break;
        }
        case 'u': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _fmt_uint(&dst, &rem, v, 10, 0, width, zero_pad, left, 0, plus);
            break;
        }
        case 'x': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _fmt_uint(&dst, &rem, v, 16, 0, width, zero_pad, left, alt, 0);
            break;
        }
        case 'X': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _fmt_uint(&dst, &rem, v, 16, 1, width, zero_pad, left, alt, 0);
            break;
        }
        case 'o': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            _fmt_uint(&dst, &rem, v, 8, 0, width, zero_pad, left, alt, 0);
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)(va_arg(ap, void *));
            _out(&dst, &rem, '0'); _out(&dst, &rem, 'x');
            _fmt_uint(&dst, &rem, v, 16, 0, width ? width : 8, 1, 0, 0, 0);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left) for (int i = 1; i < width; i++) _out(&dst, &rem, ' ');
            _out(&dst, &rem, c);
            if (left) for (int i = 1; i < width; i++) _out(&dst, &rem, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            _out_str(&dst, &rem, s, width, left);
            break;
        }
        case '%':
            _out(&dst, &rem, '%');
            break;
        case 'n':
            /* unsafe — silently skip */
            va_arg(ap, int *);
            break;
        default:
            _out(&dst, &rem, '%');
            _out(&dst, &rem, spec);
            break;
        }
    }
    if (size > 0) *dst = '\0';
    return (int)((dst - buf) + (rem <= 0 ? 1 : 0));
}

int snprintf(char *buf, int size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, 65536, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 65536, fmt, ap);
}

/* ================================================================
 * printf / fprintf
 * ================================================================ */
static char _printf_buf[1024];

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    int n = vsnprintf(_printf_buf, sizeof(_printf_buf), fmt, ap);
    fputs(_printf_buf, f);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf(stdout, fmt, ap);
}
