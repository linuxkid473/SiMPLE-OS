#include "fd.h"
#include "string.h"

void fd_table_init(fd_table_t* t) {
    for (int i = 0; i < FD_MAX; i++)
        t->fds[i].used = 0;
}

int fd_alloc(fd_table_t* t, uint16_t dir_cluster, const char* name,
             uint32_t flags, uint32_t size) {
    for (int i = FD_MIN_USER; i < FD_MAX; i++) {
        if (!t->fds[i].used) {
            t->fds[i].used        = 1;
            t->fds[i].flags       = flags;
            t->fds[i].dir_cluster = dir_cluster;
            t->fds[i].offset      = 0;
            t->fds[i].size        = size;
            int j = 0;
            while (name[j] && j < 12) {
                t->fds[i].name[j] = name[j];
                j++;
            }
            t->fds[i].name[j] = '\0';
            return i;
        }
    }
    return -(int)EMFILE;
}

file_descriptor_t* fd_get(fd_table_t* t, int fd) {
    if (fd < FD_MIN_USER || fd >= FD_MAX)
        return (file_descriptor_t*)0;
    if (!t->fds[fd].used)
        return (file_descriptor_t*)0;
    return &t->fds[fd];
}

int fd_close(fd_table_t* t, int fd) {
    if (fd < FD_MIN_USER || fd >= FD_MAX)
        return -(int)EBADF;
    if (!t->fds[fd].used)
        return -(int)EBADF;
    t->fds[fd].used = 0;
    return 0;
}
