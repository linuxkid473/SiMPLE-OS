int   fork(void);
int   wait(void);
int   write(const char *buf, int len);
void  exit(int code);

static void write_str(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

static void write_int(int v) {
    char buf[12];
    int  i = 10;
    buf[11] = '\0';
    if (v == 0) { write("0", 1); return; }
    int neg = (v < 0);
    if (neg) v = -v;
    while (v && i >= 0) { buf[i--] = '0' + (v % 10); v /= 10; }
    if (neg) buf[i--] = '-';
    write(buf + i + 1, 10 - i);
}

void _start(void) {
    int pid = fork();

    if (pid < 0) {
        write_str("FAIL: fork returned negative\n");
        exit(1);
    }

    if (pid == 0) {
        /* ---- CHILD ---- */
        write_str("[child] hello from child\n");
        exit(42);
    }

    /* ---- PARENT ---- */
    write_str("[parent] forked child pid=");
    write_int(pid);
    write_str("\n");

    int code = wait();

    write_str("[parent] child exited with code=");
    write_int(code);
    write_str("\n");

    if (code == 42)
        write_str("[parent] PASS\n");
    else
        write_str("[parent] FAIL: unexpected exit code\n");

    exit(0);
}
