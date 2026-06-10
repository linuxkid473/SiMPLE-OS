#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

/* File I/O */
ssize_t read(int fd, void *buf, size_t len);
ssize_t write(int fd, const void *buf, size_t len);
int     close(int fd);
int     dup(int fd);
int     dup2(int oldfd, int newfd);
off_t   lseek(int fd, off_t offset, int whence);

/* Process */
pid_t   fork(void);
int     execve(const char *path, char *const argv[], char *const envp[]);
int     execvp(const char *file, char *const argv[]);
int     exec(const char *path);
void    _exit(int code) __attribute__((noreturn));

/* Process info */
pid_t   getpid(void);
pid_t   getppid(void);
pid_t   getpgrp(void);
int     setpgid(int pid, int pgid);
int     setsid(void);

/* Filesystem */
int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int     unlink(const char *path);
int     rmdir(const char *path);
int     access(const char *path, int mode);
int     ftruncate(int fd, off_t length);

/* Misc */
unsigned sleep(unsigned seconds);
int      usleep(unsigned usec);
int      yield(void);
int      pipe(int fds[2]);
int      isatty(int fd);
void    *sbrk(int increment);

/* Standard fd numbers */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* seek whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* access mode */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#endif
