#ifndef SIMPLE_PIPE_H
#define SIMPLE_PIPE_H
#include "types.h"

#define PIPE_BUF_SIZE  4096
#define PIPE_MAX       16

typedef struct {
    uint8_t  buf[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;       /* bytes currently in ring */
    int      reader_open; /* # of read-end fds referencing this pipe */
    int      writer_open; /* # of write-end fds referencing this pipe */
    int      used;        /* 1 if this slot is in use */
} pipe_t;

extern pipe_t g_pipes[PIPE_MAX];

void    pipe_init(void);
int     pipe_alloc(void);    /* returns pipe index or -1 */
void    pipe_release(int idx, int is_write_end);
int     pipe_read(int idx, char *buf, uint32_t len);
int     pipe_write(int idx, const char *buf, uint32_t len);
int     pipe_read_avail(int idx);

#endif
