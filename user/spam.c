int write(const char*, int);

void _start(void) {
    write("spam 1\n", 7);
    write("spam 2\n", 7);
    write("spam 3\n", 7);
    return;
}
