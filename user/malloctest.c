// user/malloctest.c — verify userspace malloc/free backed by SYS_SBRK

int   write(const char *buf, int len);
void  exit(int code);
void *malloc(unsigned int size);
void  free(void *ptr);

static void print(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

void _start(void) {
    /* Allocate three buffers of different sizes. */
    char *a = (char *)malloc(16);
    char *b = (char *)malloc(64);
    char *c = (char *)malloc(256);

    if (!a || !b || !c) {
        print("FAIL: malloc returned NULL\n");
        exit(1);
    }

    /* Verify allocations don't overlap. */
    if (b <= a || c <= b) {
        print("FAIL: allocations overlap\n");
        exit(1);
    }

    /* Write test patterns. */
    a[0] = 'H'; a[1] = 'E'; a[2] = 'L'; a[3] = 'L'; a[4] = 'O'; a[5] = '\0';

    int i;
    for (i = 0; i < 63; i++) b[i] = 'B';
    b[63] = '\0';

    for (i = 0; i < 255; i++) c[i] = 'C';
    c[255] = '\0';

    /* Read back and verify. */
    if (a[0] != 'H' || a[1] != 'E' || a[2] != 'L' || a[3] != 'L' || a[4] != 'O') {
        print("FAIL: buffer a corrupted\n");
        exit(1);
    }

    int ok = 1;
    for (i = 0; i < 63; i++) {
        if (b[i] != 'B') { ok = 0; break; }
    }
    if (!ok) {
        print("FAIL: buffer b corrupted\n");
        exit(1);
    }

    ok = 1;
    for (i = 0; i < 255; i++) {
        if (c[i] != 'C') { ok = 0; break; }
    }
    if (!ok) {
        print("FAIL: buffer c corrupted\n");
        exit(1);
    }

    /* Free all buffers. */
    free(a);
    free(b);
    free(c);

    print("MALLOC TEST PASSED\n");
    exit(0);
}
