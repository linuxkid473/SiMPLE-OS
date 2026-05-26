#ifndef SIMPLE_FD_H
#define SIMPLE_FD_H
#include "types.h"

/* ---- open() flags (match Linux/POSIX) ---- */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_ACCMODE  0x0003
#define O_CREAT    0x0040
#define O_EXCL     0x0080
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_NONBLOCK 0x0800

/* Legacy aliases used by old SiMPLE code */
#define O_READ   O_RDONLY
#define O_WRITE  O_WRONLY
#define O_CREATE O_CREAT

/* fcntl commands */
#define F_DUPFD   0
#define F_GETFD   1
#define F_SETFD   2
#define F_GETFL   3
#define F_SETFL   4
#define FD_CLOEXEC 1

/* seek whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* poll events */
#define POLLIN   0x0001
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

/* fd types */
typedef enum {
    FD_NONE    = 0,
    FD_FILE    = 1,   /* FAT16 file */
    FD_PIPE_R  = 2,   /* read end of pipe */
    FD_PIPE_W  = 3,   /* write end of pipe */
    FD_TTY     = 4,   /* terminal (stdin/stdout/stderr) */
} fd_type_t;

#define FD_MAX     64
#define MAX_FDS    FD_MAX

/* Legacy errno codes still used in syscall.c */
#define ENOENT   2
#define EIO      5
#define ENOEXEC  8
#define EBADF    9
#define EACCES   13
#define EMFILE   24
#define EISDIR   21
#define ENOSPC   28
#define EINVAL   22

typedef struct {
    fd_type_t  type;
    uint32_t   flags;     /* O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_NONBLOCK */
    int        cloexec;   /* FD_CLOEXEC: close on exec */
    union {
        struct {           /* FD_FILE */
            uint16_t dir_cluster;
            char     name[13];
            uint32_t offset;
            uint32_t size;
        } file;
        struct {           /* FD_PIPE_R / FD_PIPE_W */
            int pipe_idx;
        } pipe;
    };
} file_desc_t;

/* Per-process fd table */
typedef struct {
    file_desc_t fds[FD_MAX];
} fd_table_t;

/* Compatibility alias for old code that uses file_descriptor_t */
typedef file_desc_t file_descriptor_t;

void         fd_table_init(fd_table_t *t);
void         fd_table_clone(fd_table_t *dst, const fd_table_t *src, int close_cloexec);
int          fd_alloc_file(fd_table_t *t, uint16_t dir_cluster, const char *name,
                           uint32_t flags, uint32_t size);
int          fd_alloc_pipe(fd_table_t *t, int pipe_idx, int is_write);
int          fd_alloc_tty(fd_table_t *t, uint32_t flags);
file_desc_t *fd_get(fd_table_t *t, int fd);
int          fd_close(fd_table_t *t, int fd);
int          fd_dup(fd_table_t *t, int oldfd);
int          fd_dup2(fd_table_t *t, int oldfd, int newfd);
int          fd_alloc(fd_table_t *t, uint16_t dir_cluster, const char *name,
                      uint32_t flags, uint32_t size);  /* legacy alias */

#endif
