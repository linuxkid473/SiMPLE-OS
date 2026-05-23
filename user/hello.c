// user/malloctest.c
#include <stddef.h>
#include <stdint.h>

#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_SBRK    15

// Syscall wrappers
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

// Minimal malloc/free
typedef struct {
    size_t size;
} alloc_header_t;

static void *malloc(size_t size) {
    if (size == 0) return 0;
    
    // Request size + header from sbrk
    size_t total = size + sizeof(alloc_header_t);
    void *ptr = (void *)syscall1(SYS_SBRK, (int)total);
    
    if (ptr == (void *)-1) return 0;
    
    // Store size in header
    alloc_header_t *header = (alloc_header_t *)ptr;
    header->size = size;
    
    // Return pointer after header
    return (void *)((char *)ptr + sizeof(alloc_header_t));
}

static void free(void *ptr) {
    // For now, just a no-op
    // Real implementation would track free list
    (void)ptr;
}

// Utilities
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

static void memcpy(void *dst, const void *src, size_t size) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static int memcmp(const void *a, const void *b, size_t size) {
    const char *ca = (const char *)a;
    const char *cb = (const char *)b;
    for (size_t i = 0; i < size; i++) {
        if (ca[i] != cb[i]) return ca[i] - cb[i];
    }
    return 0;
}

void _start(void) {
    print("=== malloc test ===\n");

    // Test 1: allocate and write to first buffer
    print("test 1: allocate 32 bytes\n");
    char *buf1 = (char *)malloc(32);
    if (!buf1) {
        print("FAILED: malloc returned null\n");
        syscall1(SYS_EXIT, 1);
    }
    const char *str1 = "hello from malloc";
    memcpy(buf1, str1, 16);
    print("wrote: ");
    syscall2(SYS_WRITE, (int)buf1, 16);
    print("\n");

    // Test 2: allocate second buffer
    print("test 2: allocate 64 bytes\n");
    char *buf2 = (char *)malloc(64);
    if (!buf2) {
        print("FAILED: malloc returned null\n");
        syscall1(SYS_EXIT, 1);
    }
    const char *str2 = "second allocation";
    memcpy(buf2, str2, 16);
    print("wrote: ");
    syscall2(SYS_WRITE, (int)buf2, 16);
    print("\n");

    // Test 3: allocate third buffer
    print("test 3: allocate 16 bytes\n");
    char *buf3 = (char *)malloc(16);
    if (!buf3) {
        print("FAILED: malloc returned null\n");
        syscall1(SYS_EXIT, 1);
    }
    const char *str3 = "tiny";
    memcpy(buf3, str3, 4);
    print("wrote: ");
    syscall2(SYS_WRITE, (int)buf3, 4);
    print("\n");

    // Test 4: verify data integrity
    print("test 4: verify data integrity\n");
    if (memcmp(buf1, "hello from malloc", 16) != 0) {
        print("FAILED: buf1 corrupted\n");
        syscall1(SYS_EXIT, 1);
    }
    if (memcmp(buf2, "second allocation", 16) != 0) {
        print("FAILED: buf2 corrupted\n");
        syscall1(SYS_EXIT, 1);
    }
    if (memcmp(buf3, "tiny", 4) != 0) {
        print("FAILED: buf3 corrupted\n");
        syscall1(SYS_EXIT, 1);
    }
    print("all buffers intact\n");

    // Test 5: free (no-op for now)
    print("test 5: free buffers\n");
    free(buf1);
    free(buf2);
    free(buf3);
    print("freed\n");

    print("MALLOC TEST PASSED\n");
    syscall1(SYS_EXIT, 0);
}