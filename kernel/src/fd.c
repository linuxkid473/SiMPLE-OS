/*
 * fd.c — file descriptor table management.
 */
#include "fd.h"
#include "pipe.h"
#include "posix_errno.h"
#include "types.h"

void fd_table_init(fd_table_t *t) {
    for (int i = 0; i < FD_MAX; i++)
        t->fds[i].type = FD_NONE;
}

void fd_table_clone(fd_table_t *dst, const fd_table_t *src, int close_cloexec) {
    for (int i = 0; i < FD_MAX; i++) {
        dst->fds[i] = src->fds[i];
        if (close_cloexec && src->fds[i].type != FD_NONE && src->fds[i].cloexec) {
            /* Close this fd in child */
            if (src->fds[i].type == FD_PIPE_R)
                pipe_release(src->fds[i].pipe.pipe_idx, 0);
            else if (src->fds[i].type == FD_PIPE_W)
                pipe_release(src->fds[i].pipe.pipe_idx, 1);
            dst->fds[i].type = FD_NONE;
        } else {
            /* Bump pipe refcounts for inherited fds */
            if (src->fds[i].type == FD_PIPE_R) {
                int idx = src->fds[i].pipe.pipe_idx;
                if (idx >= 0 && idx < PIPE_MAX) g_pipes[idx].reader_open++;
            } else if (src->fds[i].type == FD_PIPE_W) {
                int idx = src->fds[i].pipe.pipe_idx;
                if (idx >= 0 && idx < PIPE_MAX) g_pipes[idx].writer_open++;
            }
        }
    }
}

static int find_free_fd(fd_table_t *t, int min_fd) {
    for (int i = min_fd; i < FD_MAX; i++)
        if (t->fds[i].type == FD_NONE) return i;
    return -1;
}

int fd_alloc_file(fd_table_t *t, uint16_t dir_cluster, const char *name,
                  uint32_t flags, uint32_t size) {
    int fd = find_free_fd(t, 0);
    if (fd < 0) return -EMFILE;
    file_desc_t *f = &t->fds[fd];
    f->type              = FD_FILE;
    f->flags             = flags;
    f->cloexec           = 0;
    f->file.dir_cluster  = dir_cluster;
    f->file.offset       = 0;
    f->file.size         = size;
    int i = 0;
    while (name[i] && i < 12) { f->file.name[i] = name[i]; i++; }
    f->file.name[i] = '\0';
    return fd;
}

int fd_alloc_pipe(fd_table_t *t, int pipe_idx, int is_write) {
    int fd = find_free_fd(t, 0);
    if (fd < 0) return -EMFILE;
    file_desc_t *f = &t->fds[fd];
    f->type          = is_write ? FD_PIPE_W : FD_PIPE_R;
    f->flags         = is_write ? O_WRONLY : O_RDONLY;
    f->cloexec       = 0;
    f->pipe.pipe_idx = pipe_idx;
    return fd;
}

int fd_alloc_tty(fd_table_t *t, uint32_t flags) {
    int fd = find_free_fd(t, 0);
    if (fd < 0) return -EMFILE;
    file_desc_t *f = &t->fds[fd];
    f->type    = FD_TTY;
    f->flags   = flags;
    f->cloexec = 0;
    return fd;
}

/* Legacy wrapper */
int fd_alloc(fd_table_t *t, uint16_t dir_cluster, const char *name,
             uint32_t flags, uint32_t size) {
    return fd_alloc_file(t, dir_cluster, name, flags, size);
}

file_desc_t *fd_get(fd_table_t *t, int fd) {
    if (fd < 0 || fd >= FD_MAX) return (file_desc_t *)0;
    if (t->fds[fd].type == FD_NONE) return (file_desc_t *)0;
    return &t->fds[fd];
}

int fd_close(fd_table_t *t, int fd) {
    if (fd < 0 || fd >= FD_MAX) return -EBADF;
    file_desc_t *f = &t->fds[fd];
    if (f->type == FD_NONE) return -EBADF;
    if (f->type == FD_PIPE_R) pipe_release(f->pipe.pipe_idx, 0);
    if (f->type == FD_PIPE_W) pipe_release(f->pipe.pipe_idx, 1);
    f->type = FD_NONE;
    return 0;
}

int fd_dup(fd_table_t *t, int oldfd) {
    if (oldfd < 0 || oldfd >= FD_MAX || t->fds[oldfd].type == FD_NONE) return -EBADF;
    int newfd = find_free_fd(t, 0);
    if (newfd < 0) return -EMFILE;
    t->fds[newfd] = t->fds[oldfd];
    t->fds[newfd].cloexec = 0;  /* dup clears CLOEXEC */
    /* bump pipe refcount */
    if (t->fds[oldfd].type == FD_PIPE_R)
        g_pipes[t->fds[oldfd].pipe.pipe_idx].reader_open++;
    if (t->fds[oldfd].type == FD_PIPE_W)
        g_pipes[t->fds[oldfd].pipe.pipe_idx].writer_open++;
    return newfd;
}

int fd_dup2(fd_table_t *t, int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= FD_MAX || t->fds[oldfd].type == FD_NONE) return -EBADF;
    if (newfd < 0 || newfd >= FD_MAX) return -EBADF;
    if (oldfd == newfd) return newfd;
    if (t->fds[newfd].type != FD_NONE) fd_close(t, newfd);
    t->fds[newfd] = t->fds[oldfd];
    t->fds[newfd].cloexec = 0;
    if (t->fds[oldfd].type == FD_PIPE_R)
        g_pipes[t->fds[oldfd].pipe.pipe_idx].reader_open++;
    if (t->fds[oldfd].type == FD_PIPE_W)
        g_pipes[t->fds[oldfd].pipe.pipe_idx].writer_open++;
    return newfd;
}
