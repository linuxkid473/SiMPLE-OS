void _start(void) {
    char msg[] = {
        'h','e','l','l','o',' ',
        'f','r','o','m',' ',
        'u','s','e','r','s','p','a','c','e','\n'
    };

    unsigned int len = 21;

    __asm__ volatile (
        "mov $1, %%eax\n"
        "mov $1, %%ebx\n"
        "mov %0, %%ecx\n"
        "mov %1, %%edx\n"
        "int $0x80\n"
        :
        : "r"(msg), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );

    for (;;);
}
