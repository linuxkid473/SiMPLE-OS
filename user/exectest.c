/* user/exectest.c
 *
 * Tests SYS_EXEC:
 *   1. Prints "before exec\n"
 *   2. Calls exec("hello.elf")
 *   3. On success: this program disappears; hello.elf runs instead
 *   4. On failure: prints "exec failed: <code>\n" and exits
 */

int write(const char *buf, int len);
void exit(int code);
int exec(const char *path);

static void puts(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(s, len);
}

static void put_int(int n) {
    if (n < 0) { puts("-"); n = -n; }
    char buf[12];
    int i = 0;
    if (n == 0) { puts("0"); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) { char c[2] = { buf[i], 0 }; puts(c); }
}

void _start(void) {
    puts("before exec\n");

    int rc = exec("hello.elf");

    /* exec only returns on failure */
    puts("exec failed: ");
    put_int(rc);
    puts("\n");

    exit(1);
}
