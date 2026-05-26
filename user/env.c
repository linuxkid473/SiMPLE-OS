/*
 * user/env.c — getenv / setenv / putenv / environ
 *
 * The kernel currently passes no environment variables; environ starts
 * as a pointer to the NULL terminator on the initial stack. Programs can
 * add variables via setenv/putenv using the static table below.
 */

char **environ = (char **)0;

/* Static table for setenv/putenv entries (max 64 vars). */
#define ENV_MAX 64
static char *_env_table[ENV_MAX + 1];
static int   _env_count    = 0;
static int   _env_init_done = 0;

static int _strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int _strncmp(const char *a, const char *b, int n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    return n < 0 ? 0 : (unsigned char)*a - (unsigned char)*b;
}
static char *_strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (char *)0;
}

extern void *sbrk(int inc);
static char *_strdup_heap(const char *s) {
    int n = _strlen(s) + 1;
    char *p = (char *)sbrk(n);
    if ((long)p == -1) return (char *)0;
    for (int i = 0; i < n; i++) p[i] = s[i];
    return p;
}

static void _env_init(void) {
    if (_env_init_done) return;
    _env_init_done = 1;
    /* Copy entries from the kernel-provided environ into our table */
    if (environ) {
        for (int i = 0; environ[i] && _env_count < ENV_MAX; i++) {
            _env_table[_env_count++] = environ[i];
        }
    }
    _env_table[_env_count] = (char *)0;
    environ = _env_table;
}

char *getenv(const char *name) {
    _env_init();
    int nlen = _strlen(name);
    for (int i = 0; i < _env_count; i++) {
        if (_strncmp(_env_table[i], name, nlen) == 0 &&
            _env_table[i][nlen] == '=')
            return _env_table[i] + nlen + 1;
    }
    return (char *)0;
}

int putenv(char *string) {
    _env_init();
    char *eq = _strchr(string, '=');
    if (!eq) return -1;
    int nlen = (int)(eq - string);
    for (int i = 0; i < _env_count; i++) {
        if (_strncmp(_env_table[i], string, nlen) == 0 &&
            _env_table[i][nlen] == '=') {
            _env_table[i] = string;
            return 0;
        }
    }
    if (_env_count >= ENV_MAX) return -1;
    _env_table[_env_count++] = string;
    _env_table[_env_count]   = (char *)0;
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    _env_init();
    int nlen = _strlen(name);
    int vlen = _strlen(value);
    for (int i = 0; i < _env_count; i++) {
        if (_strncmp(_env_table[i], name, nlen) == 0 &&
            _env_table[i][nlen] == '=') {
            if (!overwrite) return 0;
            /* Allocate new "name=value" string */
            char *p = (char *)sbrk(nlen + 1 + vlen + 1);
            if ((long)p == -1) return -1;
            int k = 0;
            for (int j = 0; j < nlen; j++) p[k++] = name[j];
            p[k++] = '=';
            for (int j = 0; j <= vlen; j++) p[k++] = value[j];
            _env_table[i] = p;
            return 0;
        }
    }
    if (_env_count >= ENV_MAX) return -1;
    char *p = _strdup_heap(name);
    if (!p) return -1;
    /* Build "name=value" */
    p = (char *)sbrk(nlen + 1 + vlen + 1);
    if ((long)p == -1) return -1;
    int k = 0;
    for (int j = 0; j < nlen; j++) p[k++] = name[j];
    p[k++] = '=';
    for (int j = 0; j <= vlen; j++) p[k++] = value[j];
    _env_table[_env_count++] = p;
    _env_table[_env_count]   = (char *)0;
    return 0;
}

int unsetenv(const char *name) {
    _env_init();
    int nlen = _strlen(name);
    for (int i = 0; i < _env_count; i++) {
        if (_strncmp(_env_table[i], name, nlen) == 0 &&
            _env_table[i][nlen] == '=') {
            /* Shift remaining entries down */
            for (int j = i; j < _env_count - 1; j++)
                _env_table[j] = _env_table[j + 1];
            _env_count--;
            _env_table[_env_count] = (char *)0;
            return 0;
        }
    }
    return 0;
}
