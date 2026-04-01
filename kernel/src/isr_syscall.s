.global isr_syscall
.extern isr_handler

.type isr_syscall, @function
isr_syscall:
    cli

    pushl $0
    pushl $128

    pusha

    push %ds
    push %es
    push %fs
    push %gs

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    mov %esp, %eax
    push %eax

    call isr_handler

    add $4, %esp

    pop %gs
    pop %fs
    pop %es
    pop %ds

    popa

    add $8, %esp

    iret
.size isr_syscall, . - isr_syscall
