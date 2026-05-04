int write(const char*, int);
void exit(void);

void _start(void) {
    write("before exit\n", 12);
    exit();
}