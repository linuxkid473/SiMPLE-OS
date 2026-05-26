/*
 * user/dirent.c — opendir / readdir / closedir / rewinddir
 *
 * Uses readdir_simple (syscall 410) which fills a flat array of
 * { char name[64]; uint8_t is_dir; uint8_t _pad[3]; uint32_t size; }.
 * We buffer all entries on open and serve them one at a time from readdir.
 */
#include "dirent.h"

/* Raw entry from readdir_simple syscall (matches kernel sys_readdir layout) */
typedef struct {
    char    name[64];
    unsigned char is_dir;
    unsigned char _pad[3];
    unsigned int  size;
} raw_dirent_t;

#define MAX_ENTRIES 256

struct _DIR {
    raw_dirent_t  entries[MAX_ENTRIES];
    int           count;
    int           pos;
    struct dirent cur;      /* scratch space for readdir() */
};

/* Opaque pool — at most 8 open directory streams */
#define DIRPOOL_SIZE 8
static struct _DIR _dir_pool[DIRPOOL_SIZE];
static int         _dir_used[DIRPOOL_SIZE];

/* Provided by libc.c */
int readdir_simple(const char *path, void *buf, int max);

static int _strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void _strcpy(char *d, const char *s) { while ((*d++ = *s++)); }
extern void *sbrk(int);

DIR *opendir(const char *path) {
    int slot = -1;
    for (int i = 0; i < DIRPOOL_SIZE; i++) {
        if (!_dir_used[i]) { slot = i; break; }
    }
    if (slot < 0) return (DIR *)0;

    struct _DIR *d = &_dir_pool[slot];
    d->count = readdir_simple(path, d->entries, MAX_ENTRIES);
    if (d->count < 0) d->count = 0;
    d->pos = 0;
    _dir_used[slot] = 1;
    return d;
}

struct dirent *readdir(DIR *d) {
    if (!d || d->pos >= d->count) return (struct dirent *)0;
    raw_dirent_t *r = &d->entries[d->pos++];
    d->cur.d_ino  = (ino_t)d->pos;
    d->cur.d_type = r->is_dir ? DT_DIR : DT_REG;
    /* copy name (max 255 chars) */
    int n = _strlen(r->name);
    if (n > 255) n = 255;
    for (int i = 0; i < n; i++) d->cur.d_name[i] = r->name[i];
    d->cur.d_name[n] = '\0';
    return &d->cur;
}

int closedir(DIR *d) {
    if (!d) return -1;
    for (int i = 0; i < DIRPOOL_SIZE; i++) {
        if (&_dir_pool[i] == d) {
            _dir_used[i] = 0;
            return 0;
        }
    }
    return -1;
}

void rewinddir(DIR *d) {
    if (d) d->pos = 0;
}
