/*
 * pipe.c — kernel pipe implementation using ring buffers.
 */
#include "pipe.h"
#include "posix_errno.h"
#include "process.h"
#include "signal.h"
#include "types.h"

pipe_t g_pipes[PIPE_MAX];

void pipe_init(void) {
    for (int i = 0; i < PIPE_MAX; i++) {
        g_pipes[i].used        = 0;
        g_pipes[i].read_pos    = 0;
        g_pipes[i].write_pos   = 0;
        g_pipes[i].count       = 0;
        g_pipes[i].reader_open = 0;
        g_pipes[i].writer_open = 0;
    }
}

int pipe_alloc(void) {
    for (int i = 0; i < PIPE_MAX; i++) {
        if (!g_pipes[i].used) {
            g_pipes[i].used        = 1;
            g_pipes[i].read_pos    = 0;
            g_pipes[i].write_pos   = 0;
            g_pipes[i].count       = 0;
            g_pipes[i].reader_open = 1;
            g_pipes[i].writer_open = 1;
            return i;
        }
    }
    return -1;
}

void pipe_release(int idx, int is_write_end) {
    if (idx < 0 || idx >= PIPE_MAX || !g_pipes[idx].used) return;
    pipe_t *p = &g_pipes[idx];
    if (is_write_end) {
        if (p->writer_open > 0) p->writer_open--;
    } else {
        if (p->reader_open > 0) p->reader_open--;
    }
    if (p->reader_open == 0 && p->writer_open == 0) {
        p->used = 0;
    }
}

int pipe_read_avail(int idx) {
    if (idx < 0 || idx >= PIPE_MAX) return 0;
    return (int)g_pipes[idx].count;
}

int pipe_read(int idx, char *buf, uint32_t len) {
    if (idx < 0 || idx >= PIPE_MAX || !g_pipes[idx].used) return -1;
    pipe_t *p = &g_pipes[idx];

    /* Check for data */
    if (p->count == 0) {
        if (p->writer_open == 0) return 0;  /* EOF */
        /* Check for pending signals */
        if (current_proc >= 0) {
            if (proc_table[current_proc].sig_pending &
                ~proc_table[current_proc].sig_mask)
                return -EINTR;
        }
        /* No data available yet */
        return 0;
    }

    uint32_t n = (len < p->count) ? len : p->count;
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = (char)p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count -= n;
    return (int)n;
}

int pipe_write(int idx, const char *buf, uint32_t len) {
    if (idx < 0 || idx >= PIPE_MAX || !g_pipes[idx].used) return -1;
    pipe_t *p = &g_pipes[idx];

    if (p->reader_open == 0) {
        /* Broken pipe */
        if (current_proc >= 0)
            proc_send_signal(proc_table[current_proc].pid, SIGPIPE);
        return -EPIPE;
    }

    if (p->count >= PIPE_BUF_SIZE) {
        /* Buffer full */
        return 0;
    }

    uint32_t space = PIPE_BUF_SIZE - p->count;
    uint32_t to_write = (len < space) ? len : space;
    for (uint32_t i = 0; i < to_write; i++) {
        p->buf[p->write_pos] = (uint8_t)buf[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
    }
    p->count += to_write;
    return (int)to_write;
}
