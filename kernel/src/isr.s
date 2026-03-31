.global isr21
.extern isr_handler

.type isr21, @function
isr21:
    cli

    push %gs
    push %fs
    push %es
    push %ds

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl $0
    pushl $48

    pusha

    mov %esp, %eax
    push %eax

    call isr_handler

    add $4, %esp

    popa

    add $8, %esp

    pop %ds
    pop %es
    pop %fs
    pop %gs

    iret
.size isr21, . - isr21
