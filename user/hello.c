void _start(void) {
    const char* msg = "hello from userspace\n";
    unsigned int len = 21;

    __asm__ volatile(
        "mov $1, %%eax\n"
        "mov $1, %%ebx\n"
        "mov %0, %%ecx\n"
        "mov %1, %%edx\n"
        "int $0x80\n"
        :
        : "r"(msg), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );

    return;  // ← THIS is the fix
}
