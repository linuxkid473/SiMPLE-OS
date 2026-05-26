/*
 * user/posixtest.c — smoke test for the POSIX substrate.
 * Tests: printf/fprintf, argc/argv, file I/O, fork/waitpid, pipe.
 */
#include "stdio.h"

int fork(void);
int waitpid(int pid, int *status, int opts);
int pipe(int fds[2]);
int read(int fd, void *buf, int len);
int write(int fd, const void *buf, int len);
int close(int fd);
int getpid(void);
void exit(int code);

int main(int argc, char **argv, char **envp) {
    (void)envp;

    printf("=== SiMPLE OS POSIX test ===\n");
    printf("argc = %d\n", argc);
    if (argc > 0) printf("argv[0] = %s\n", argv[0]);

    /* sprintf / snprintf */
    char buf[64];
    snprintf(buf, sizeof(buf), "pid = %d", getpid());
    printf("%s\n", buf);

    /* File I/O */
    FILE *f = fopen("posix.txt", "w");
    if (f) {
        fprintf(f, "hello from posixtest\n");
        fclose(f);
        printf("wrote posix.txt\n");
        f = fopen("posix.txt", "r");
        if (f) {
            char line[64];
            if (fgets(line, sizeof(line), f))
                printf("read back: %s", line);
            fclose(f);
        }
    } else {
        printf("fopen(w) failed\n");
    }

    /* pipe */
    int pp[2];
    if (pipe(pp) == 0) {
        int pid = fork();
        if (pid == 0) {
            /* child: write to pipe and exit */
            close(pp[0]);
            const char *msg = "hello pipe\n";
            write(pp[1], msg, 11);
            close(pp[1]);
            exit(0);
        } else if (pid > 0) {
            /* parent: read from pipe */
            close(pp[1]);
            char rbuf[32];
            int n = read(pp[0], rbuf, sizeof(rbuf) - 1);
            close(pp[0]);
            int status = 0;
            waitpid(pid, &status, 0);
            if (n > 0) {
                rbuf[n] = '\0';
                printf("pipe recv: %s", rbuf);
            }
        }
    } else {
        printf("pipe() failed\n");
    }

    printf("=== all tests done ===\n");
    return 0;
}
