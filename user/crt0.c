/*
 * user/crt0.c — C runtime entry point for POSIX programs.
 *
 * The kernel plants a POSIX-style initial stack (build_posix_stack):
 *   [esp]   = argc
 *   [esp+4] = argv[0]  (program path)
 *   [esp+8] = NULL     (argv terminator)
 *   [esp+12]= NULL     (envp terminator)
 *   [esp+16]= AT_NULL  (auxv)
 *
 * _start reads argc/argv/envp from the stack and calls main().
 * Programs that link crt0.c must define main() instead of _start().
 */

void exit(int code);
int  main(int argc, char **argv, char **envp);

__attribute__((naked)) void _start(void) {
    __asm__ volatile (
        /* esp points to argc */
        "movl  (%%esp),  %%eax\n\t"   /* eax = argc */
        "leal 4(%%esp),  %%ecx\n\t"   /* ecx = argv (= &argv[0]) */
        "leal 4(%%ecx, %%eax, 4), %%edx\n\t" /* edx = envp (past argv+NULL) */

        /* align stack to 16 bytes before call (System V i386 ABI) */
        "andl  $-16,    %%esp\n\t"
        "subl  $4,      %%esp\n\t"    /* 12 bytes of args + 4 = 16-aligned call */

        "pushl %%edx\n\t"   /* envp */
        "pushl %%ecx\n\t"   /* argv */
        "pushl %%eax\n\t"   /* argc */
        "call  main\n\t"
        /* main returned — call exit(eax) */
        "pushl %%eax\n\t"
        "call  exit\n\t"
        "1: hlt\n\t"
        "jmp 1b\n\t"
        : : : "memory"
    );
}
