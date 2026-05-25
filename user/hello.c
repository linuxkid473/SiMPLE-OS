/* user/test_each.c - individual verbose test for each syscall 13-19 with 5s delays */
#include "syscall.h"

int write(const char *buf, int len);
void exit(int code);
int open(const char *path, int flags);
int close(int fd);
int fd_write(int fd, const void *buf, int len);
int fork(void);
int wait(void);

#define O_WRITE  2
#define O_CREATE 4

static void print(const char *s) { int l=0; while(s[l]) l++; write(s,l); }
static void println(const char *s) { print(s); print("\n"); }
static char *itoa(int n, char *buf) {
    if(!n){buf[0]='0';buf[1]=0;return buf;}
    int neg=0,i=0; char t[16];
    if(n<0){neg=1;n=-n;}
    while(n){t[i++]='0'+(n%10);n/=10;}
    if(neg)t[i++]='-';
    int j=0; while(i--)buf[j++]=t[i]; buf[j]=0; return buf;
}
static void print_int(int n) { char b[16]; print(itoa(n,b)); }
static void print_uint(unsigned int n) { char b[16]; print(itoa((int)n,b)); }

static void delay_5s(void) {
    println("  [waiting 5 seconds...]");
    sys_sleep(500);  /* 500 ticks at 100 Hz = 5 seconds */
}

/* ═══════════════════════════════════════════════════════════════
   TEST 13: SYS_GETPID
   ═════════════════════════════════════════════════════════════════ */
void test_13_getpid(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 13: SYS_GETPID       ║");
    println("╚═══════════════════════════╝");
    
    int pid = getpid();
    print("  returned: ");
    print_int(pid);
    println("");
    
    if (pid > 0) {
        print("  [PASS] getpid returned valid pid ");
        print_int(pid);
        println("");
    } else {
        println("  [FAIL] getpid returned invalid pid");
    }
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   TEST 14: SYS_SLEEP
   ═════════════════════════════════════════════════════════════════ */
void test_14_sleep(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 14: SYS_SLEEP        ║");
    println("╚═══════════════════════════╝");
    
    unsigned int before = getticks();
    print("  before: ");
    print_uint(before);
    println("");
    
    println("  sleeping 30 ticks...");
    sys_sleep(30);
    
    unsigned int after = getticks();
    print("  after:  ");
    print_uint(after);
    println("");
    
    unsigned int delta = after - before;
    print("  delta:  ");
    print_uint(delta);
    println("");
    
    if (delta >= 30) {
        print("  [PASS] slept at least 30 ticks (got ");
        print_uint(delta);
        println(")");
    } else {
        print("  [FAIL] woke too early (only ");
        print_uint(delta);
        println(" ticks)");
    }
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   TEST 16: SYS_STAT
   ═════════════════════════════════════════════════════════════════ */
void test_16_stat(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 16: SYS_STAT         ║");
    println("╚═══════════════════════════╝");
    
    println("  creating TEST13.TXT...");
    int fd = open("TEST13.TXT", O_WRITE | O_CREATE);
    if (fd < 0) {
        println("  [FAIL] could not create file");
        delay_5s();
        return;
    }
    fd_write(fd, "stat test data", 14);
    close(fd);
    println("  file created (14 bytes)");
    
    println("  calling stat(\"TEST13.TXT\", &st)...");
    stat_t st;
    int r = stat("TEST13.TXT", &st);
    
    print("  result: ");
    print_int(r);
    println("");
    print("  exists: ");
    print_int(st.exists);
    println("");
    print("  is_dir: ");
    print_int(st.is_dir);
    println("");
    print("  size:   ");
    print_uint(st.size);
    println("");
    
    if (r == 0 && st.exists && st.size == 14) {
        println("  [PASS] stat found file with correct size");
    } else {
        println("  [FAIL] stat returned wrong data");
    }
    
    println("  calling stat(\"NOFILE.TXT\", &st)...");
    stat_t st2;
    int r2 = stat("NOFILE.TXT", &st2);
    print("  exists: ");
    print_int(st2.exists);
    println("");
    
    if (!st2.exists) {
        println("  [PASS] stat correctly reports file not found");
    } else {
        println("  [FAIL] stat reports non-existent file as existing");
    }
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   TEST 17: SYS_READDIR
   ═════════════════════════════════════════════════════════════════ */
void test_17_readdir(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 17: SYS_READDIR      ║");
    println("╚═══════════════════════════╝");
    
    println("  calling readdir(\"/\", entries, 32)...");
    dirent_t entries[32];
    int count = readdir("/", entries, 32);
    
    print("  returned: ");
    print_int(count);
    println(" entries");
    
    if (count <= 0) {
        println("  [FAIL] readdir returned no entries");
        delay_5s();
        return;
    }
    
    println("  entries found:");
    for (int i = 0; i < count && i < 5; i++) {
        print("    ");
        print(entries[i].is_dir ? "[DIR]  " : "[FILE] ");
        print(entries[i].name);
        print(" (");
        print_uint(entries[i].size);
        println(" bytes)");
    }
    
    if (count > 5) {
        print("    ... and ");
        print_int(count - 5);
        println(" more");
    }
    
    println("  [PASS] readdir returned entries");
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   TEST 18: SYS_RENAME
   ═════════════════════════════════════════════════════════════════ */
void test_18_rename(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 18: SYS_RENAME       ║");
    println("╚═══════════════════════════╝");
    
    println("  creating OLDNAME.TXT...");
    int fd = open("OLDNAME.TXT", O_WRITE | O_CREATE);
    if (fd < 0) {
        println("  [FAIL] could not create file");
        delay_5s();
        return;
    }
    fd_write(fd, "rename test", 11);
    close(fd);
    println("  file created");

    println("  calling rename(\"OLDNAME.TXT\", \"NEWNAME.TXT\")...");
    int r = rename("OLDNAME.TXT", "NEWNAME.TXT");
    print("  result: ");
    print_int(r);
    println("");

    if (r != 0) {
        println("  [FAIL] rename returned non-zero");
        delay_5s();
        return;
    }

    println("  checking old name...");
    stat_t old_st;
    stat("OLDNAME.TXT", &old_st);
    print("  old file exists: ");
    print_int(old_st.exists);
    println("");

    println("  checking new name...");
    stat_t new_st;
    stat("NEWNAME.TXT", &new_st);
    print("  new file exists: ");
    print_int(new_st.exists);
    println("");

    if (r == 0 && !old_st.exists && new_st.exists) {
        println("  [PASS] rename worked correctly");
    } else {
        println("  [FAIL] rename did not work as expected");
    }
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   TEST 19: SYS_GETTICKS
   ═════════════════════════════════════════════════════════════════ */
void test_19_getticks(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 19: SYS_GETTICKS     ║");
    println("╚═══════════════════════════╝");
    
    unsigned int t1 = getticks();
    print("  t1 = ");
    print_uint(t1);
    println("");
    
    volatile int x = 0;
    for (int i = 0; i < 300000; i++) x += i;
    
    unsigned int t2 = getticks();
    print("  t2 = ");
    print_uint(t2);
    println("");
    
    print("  delta = ");
    print_uint(t2 - t1);
    println("");
    
    if (t2 >= t1) {
        println("  [PASS] tick counter is monotonic");
    } else {
        println("  [FAIL] tick counter went backwards");
    }
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   TEST 11/12: SYS_FORK + SYS_WAIT
   ═════════════════════════════════════════════════════════════════ */
void test_11_12_fork_wait(void) {
    println("\n╔═══════════════════════════╗");
    println("║ TEST 11/12: FORK + WAIT   ║");
    println("╚═══════════════════════════╝");
    
    int parent_pid = getpid();
    print("  parent pid = ");
    print_int(parent_pid);
    println("");
    
    println("  calling fork()...");
    int child_pid = fork();
    
    if (child_pid == 0) {
        /* child */
        int my_pid = getpid();
        print("  [CHILD] my pid = ");
        print_int(my_pid);
        println("");
        
        println("  [CHILD] exiting with code 99...");
        exit(99);
    } else if (child_pid > 0) {
        /* parent */
        print("  [PARENT] child pid = ");
        print_int(child_pid);
        println("");
        
        println("  [PARENT] calling wait()...");
        int exit_code = wait();
        print("  [PARENT] child exited with code ");
        print_int(exit_code);
        println("");
        
        if (child_pid > 0 && exit_code == 99) {
            println("  [PASS] fork and wait worked correctly");
        } else {
            println("  [FAIL] fork or wait returned wrong values");
        }
    } else {
        println("  [FAIL] fork returned negative (failed)");
    }
    
    delay_5s();
}

/* ═══════════════════════════════════════════════════════════════
   MAIN
   ═════════════════════════════════════════════════════════════════ */
void _start(void) {
    println("\n╔════════════════════════════════════╗");
    println("║  SiMPLE OS Syscalls 13-19 Tests   ║");
    println("║   (5 second delays between tests)  ║");
    println("╚════════════════════════════════════╝");
    
    test_13_getpid();
    test_14_sleep();
    test_16_stat();
    test_17_readdir();
    test_18_rename();
    test_19_getticks();
    test_11_12_fork_wait();
    
    println("\n╔════════════════════════════════════╗");
    println("║         ALL TESTS DONE             ║");
    println("╚════════════════════════════════════╝\n");
    
    exit(0);
}