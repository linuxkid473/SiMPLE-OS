#ifndef SIMPLE_FD_H
#define SIMPLE_FD_H

#include "types.h"

/* open flags */
#define O_READ   (1U)
#define O_WRITE  (2U)
#define O_CREATE (4U)

/* errno codes (POSIX subset) */
#define ENOENT   2
#define EIO      5
#define EBADF    9
#define EACCES   13
#define EMFILE   24
#define EISDIR   21
#define ENOSPC   28
#define EINVAL   22

/* seek whence constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* descriptor table sizing */
#define FD_MAX      8
#define FD_MIN_USER 3   /* 0-2 reserved (stdin/stdout/stderr ABI slots) */

typedef struct {
    int      used;
    uint32_t flags;
    uint16_t dir_cluster;
    char     name[13];
    uint32_t offset;
    uint32_t size;
} file_descriptor_t;

typedef struct {
    file_descriptor_t fds[FD_MAX];
} fd_table_t;

void               fd_table_init(fd_table_t* t);
int                fd_alloc(fd_table_t* t, uint16_t dir_cluster,
                            const char* name, uint32_t flags, uint32_t size);
file_descriptor_t* fd_get(fd_table_t* t, int fd);
int                fd_close(fd_table_t* t, int fd);

#endif
