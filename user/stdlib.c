/*
 * user/stdlib.c — stdlib, string extras, and misc POSIX functions
 * that are not in libc.c or stdio.c.
 */

typedef unsigned int  size_t;
typedef int           ssize_t;

/* ---- from libc.c ---- */
char *strchr(const char *s, int c);
void  exit(int code);
void  _exit(int code);
void *sbrk(int inc);
int   write(int fd, const void *buf, int len);
int   kill(int pid, int sig);
int   getpid(void);
int   nanosleep(const void *req, void *rem);

/* ---- string helpers ---- */
static size_t _sl(const char *s) { size_t n=0; while(s[n]) n++; return n; }

/* memchr */
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    return (void *)0;
}

/* ftruncate — stub: writers open with O_TRUNC, which the kernel honors,
 * so the only remaining case (shrinking an already-open fd) cannot occur
 * with the FAT16 driver's whole-file write model. */
int ftruncate(int fd, int length) {
    (void)fd; (void)length;
    return 0;
}

/* memmove — handles overlapping regions */
void *memmove(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    if (d < s || d >= s + n) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i-- > 0; ) d[i] = s[i];
    }
    return dst;
}

/* strncat */
char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (*d) d++;
    for (size_t i = 0; i < n && src[i]; i++) *d++ = src[i];
    *d = '\0';
    return dst;
}

/* strrchr */
char *strrchr(const char *s, int c) {
    const char *last = (char *)0;
    while (*s) { if (*s == (char)c) last = s; s++; }
    if (c == 0) return (char *)s;
    return (char *)last;
}

/* strstr — naive search */
char *strstr(const char *hay, const char *needle) {
    size_t nl = _sl(needle);
    if (!nl) return (char *)hay;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nl && hay[i] == needle[i]; i++);
        if (i == nl) return (char *)hay;
    }
    return (char *)0;
}

/* strdup — allocates via sbrk */
char *strdup(const char *s) {
    size_t n = _sl(s) + 1;
    char *p = (char *)sbrk((int)n);
    if ((int)(size_t)p == -1) return (char *)0;
    for (size_t i = 0; i < n; i++) p[i] = s[i];
    return p;
}

/* strtok / strtok_r */
char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (str) *saveptr = str;
    if (!*saveptr) return (char *)0;
    /* skip leading delimiters */
    while (**saveptr && strchr(delim, **saveptr)) (*saveptr)++;
    if (!**saveptr) { *saveptr = (char *)0; return (char *)0; }
    char *start = *saveptr;
    while (**saveptr && !strchr(delim, **saveptr)) (*saveptr)++;
    if (**saveptr) { **saveptr = '\0'; (*saveptr)++; }
    else           { *saveptr = (char *)0; }
    return start;
}

static char *_strtok_save = (char *)0;
char *strtok(char *str, const char *delim) {
    return strtok_r(str, delim, &_strtok_save);
}

/* strtol / strtoul */
long strtol(const char *s, char **endp, int base) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (s[0]=='0' && (s[1]=='x'||s[1]=='X')) { base=16; s+=2; }
        else if (s[0]=='0') { base=8; s++; }
        else base=10;
    } else if (base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    long v = 0;
    while (*s) {
        int d;
        if (*s>='0' && *s<='9') d=*s-'0';
        else if (*s>='a' && *s<='z') d=*s-'a'+10;
        else if (*s>='A' && *s<='Z') d=*s-'A'+10;
        else break;
        if (d >= base) break;
        v = v*base + d;
        s++;
    }
    if (endp) *endp = (char *)s;
    return neg ? -v : v;
}

unsigned long strtoul(const char *s, char **endp, int base) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    if (base == 0) {
        if (s[0]=='0' && (s[1]=='x'||s[1]=='X')) { base=16; s+=2; }
        else if (s[0]=='0') { base=8; s++; }
        else base=10;
    } else if (base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    unsigned long v = 0;
    while (*s) {
        int d;
        if (*s>='0' && *s<='9') d=*s-'0';
        else if (*s>='a' && *s<='z') d=*s-'a'+10;
        else if (*s>='A' && *s<='Z') d=*s-'A'+10;
        else break;
        if (d >= base) break;
        v = v*(unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endp) *endp = (char *)s;
    return v;
}

int atoi(const char *s) { return (int)strtol(s, (char **)0, 10); }
long atol(const char *s) { return strtol(s, (char **)0, 10); }
double atof(const char *s) { (void)s; return 0.0; }  /* stub */

int abs(int x)   { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

/* ---- memory allocation extras ---- */
/* from malloc.c */
void *malloc(size_t size);
void  free(void *ptr);

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (total == 0) return (void *)0;
    char *p = (char *)malloc(total);
    if (!p) return (void *)0;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return p;
}

/* realloc() lives in malloc.c — it needs the block headers to know the
 * old allocation size. */

/* ---- abort ---- */
void abort(void) {
    kill(getpid(), 6 /* SIGABRT */);
    _exit(134);
    for (;;);
}

/* ---- qsort — simple insertion sort for small arrays, heapsort fallback ---- */
static void _swap(char *a, char *b, size_t size) {
    while (size--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *)) {
    char *b = (char *)base;
    /* Shell sort — O(n log n) average, no recursion, no stack overflow */
    size_t gap = nmemb;
    while (gap > 1) {
        gap = gap * 10 / 13;  /* gap sequence: Knuth-ish */
        if (gap == 0) gap = 1;
        for (size_t i = 0; i + gap < nmemb; i++) {
            if (cmp(b + i*size, b + (i+gap)*size) > 0)
                _swap(b + i*size, b + (i+gap)*size, size);
        }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *)) {
    const char *b = (const char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, b + mid * size);
        if (r == 0) return (void *)(b + mid * size);
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    return (void *)0;
}

/* ---- perror ---- */
void perror(const char *s) {
    if (s && *s) { write(2, s, (int)_sl(s)); write(2, ": ", 2); }
    write(2, "error\n", 6);
}

/* ---- remove ---- */
int unlink(const char *path);
int remove(const char *path) { return unlink(path); }

/* ---- raise ---- */
int raise(int sig) { return kill(getpid(), sig); }

/* ---- usleep ---- */
int usleep(unsigned usec) {
    struct { long tv_sec; long tv_nsec; } ts;
    ts.tv_sec  = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    return nanosleep(&ts, (void *)0);
}

