
.section .text
.global _start
.type _start, @function
_start:
    cli
    pop %ebx
    mov $stack_top, %esp
    push %ebx
    call kernel_main

.hang:
    hlt
    jmp .hang

.size _start, . - _start

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:
