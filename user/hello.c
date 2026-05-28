// posixstress.c
// Comprehensive SiMPLE OS POSIX subsystem tester

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

volatile int sigint_seen = 0;

void sig_handler(int sig) {
    printf("[signal] received signal %d\n", sig);
    sigint_seen = 1;
}

void divider(const char* name) {
    printf("\n==============================\n");
    printf("%s\n", name);
    printf("==============================\n");
}

void wait5() {
    printf("\n[waiting 5 seconds]\n");
    sleep(5);
}

void test_args(int argc, char** argv, char** envp) {
    divider("ARGV / ENVP TEST");

    printf("argc = %d\n", argc);

    for(int i = 0; i < argc; i++) {
        printf("argv[%d] = '%s'\n", i, argv[i]);
    }

    int count = 0;

    while(envp[count] && count < 5) {
        printf("envp[%d] = '%s'\n", count, envp[count]);
        count++;
    }
}

void test_stdio() {
    divider("STDIO + FILE TEST");

    FILE* f = fopen("/test.txt", "w");

    if(!f) {
        printf("fopen failed\n");
        return;
    }

    fprintf(f, "Hello from stdio layer!\n");
    fprintf(f, "Number: %d\n", 12345);

    fclose(f);

    f = fopen("/test.txt", "r");

    if(!f) {
        printf("reopen failed\n");
        return;
    }

    char buf[128];

    while(fgets(buf, sizeof(buf), f)) {
        printf("READ: %s", buf);
    }

    fclose(f);
}

void test_pipe() {
    divider("PIPE + BLOCKING TEST");

    int p[2];

    if(pipe(p) < 0) {
        printf("pipe failed\n");
        return;
    }

    int pid = fork();

    if(pid == 0) {
        printf("[child] sleeping before write...\n");
        sleep(2);

        write(p[1], "abc", 3);

        printf("[child] wrote to pipe\n");

        exit(0);
    }

    char buf[4];

    memset(buf, 0, sizeof(buf));

    printf("[parent] blocking on read...\n");

    read(p[0], buf, 3);

    printf("[parent] read returned: '%s'\n", buf);

    waitpid(pid, NULL, 0);
}

void test_fork_wait() {
    divider("FORK + WAITPID TEST");

    int pid = fork();

    if(pid == 0) {
        printf("[child] pid=%d\n", getpid());

        sleep(1);

        exit(42);
    }

    int status = 0;

    int r = waitpid(pid, &status, 0);

    printf("[parent] waitpid returned %d\n", r);
    printf("[parent] raw status = %d\n", status);
}

void test_signals() {
    divider("SIGNAL TEST");

    signal(SIGINT, sig_handler);

    printf("sending SIGINT to self...\n");

    kill(getpid(), SIGINT);

    sleep(1);

    printf("sigint_seen = %d\n", sigint_seen);
}

void test_dirent() {
    divider("DIRENT TEST");

    DIR* d = opendir("/");

    if(!d) {
        printf("opendir failed\n");
        return;
    }

    struct dirent* ent;

    while((ent = readdir(d))) {
        printf("dir: %s\n", ent->d_name);
    }

    closedir(d);
}

void test_ansi() {
    divider("ANSI / VT100 TEST");

    printf("\x1b[31mRED TEXT\x1b[0m\n");
    printf("\x1b[32mGREEN TEXT\x1b[0m\n");
    printf("\x1b[34mBLUE TEXT\x1b[0m\n");

    sleep(1);

    printf("\x1b[10;10HCursor moved to row 10 col 10\n");

    sleep(1);

    printf("\x1b[2J");

    printf("Screen cleared.\n");
}

void test_malloc() {
    divider("MALLOC TEST");

    for(int i = 0; i < 10; i++) {
        char* p = malloc(128);

        if(!p) {
            printf("malloc failed at %d\n", i);
            return;
        }

        memset(p, 'A' + i, 127);

        p[127] = 0;

        printf("alloc[%d] = %.16s...\n", i, p);

        free(p);
    }

    printf("malloc/free cycle complete\n");
}

int main(int argc, char** argv, char** envp) {

    printf("\n");
    printf("====================================\n");
    printf(" SiMPLE OS POSIX SUBSYSTEM TESTER\n");
    printf("====================================\n");

    test_args(argc, argv, envp);
    wait5();

    test_stdio();
    wait5();

    test_pipe();
    wait5();

    test_fork_wait();
    wait5();

    test_signals();
    wait5();

    test_dirent();
    wait5();

    test_ansi();
    wait5();

    test_malloc();
    wait5();

    divider("ALL TESTS COMPLETE");

    while(1) {
        sleep(1);
    }

    return 0;
}