// user/forkwait.c
// Syscall numbers
#define SYS_WRITE  1
#define SYS_EXIT   2
#define SYS_FORK   11
#define SYS_WAIT   12

// Raw syscall helpers
static inline int syscall1(int num, int a) {
    int ret;
    __asm__ volatile (
        "mov %1, %%eax\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(a)
        : "eax", "ecx"
    );
    return ret;
}

static inline int syscall2(int num, int a, int b) {
    int ret;
    __asm__ volatile (
        "mov %1, %%eax\n"
        "mov %2, %%ecx\n"
        "mov %3, %%edx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(num), "r"(a), "r"(b)
        : "eax", "ecx", "edx"
    );
    return ret;
}

static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "mov %1, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(num)
        : "eax"
    );
    return ret;
}

// Minimal write/exit wrappers
static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    syscall2(SYS_WRITE, (int)s, len);
}

static void print_int(int n) {
    if (n < 0) { print("-"); n = -n; }
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (n == 0) { print("0"); return; }
    while (n > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    print(&buf[i]);
}

void _start(void) {
    print("=== forkwait test ===\n");

    int pid = syscall0(SYS_FORK);

    if (pid < 0) {
        print("fork failed\n");
        syscall1(SYS_EXIT, 1);
    }

    if (pid == 0) {
        // Child
        print("child: i am alive, pid=0 (child view)\n");
        print("child: doing some work...\n");
        print("child: exiting with code 42\n");
        syscall1(SYS_EXIT, 42);
    } else {
        // Parent
        print("parent: forked child with pid=");
        print_int(pid);
        print("\n");
        print("parent: waiting for child...\n");

        int code = syscall0(SYS_WAIT);

        print("parent: child exited with code=");
        print_int(code);
        print("\n");

        if (code == 42) {
            print("TEST PASSED\n");
        } else {
            print("TEST FAILED (unexpected exit code)\n");
        }

        syscall1(SYS_EXIT, 0);
    }
}