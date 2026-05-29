.global isr0
.global isr1
.global isr2
.global isr3
.global isr4
.global isr5
.global isr6
.global isr7
.global isr8
.global isr9
.global isr10
.global isr11
.global isr12
.global isr13
.global isr14
.global isr15
.global isr16
.global isr17
.global isr18
.global isr19
.global isr20
.global isr21
.global isr22
.global isr23
.global isr24
.global isr25
.global isr26
.global isr27
.global isr28
.global isr29
.global isr30
.global isr31
.global isr33
.global isr34
.global isr48
/* IRQ stubs — hardware interrupts remapped to 0x20-0x2F after pic_init() */
.global isr32

.extern isr_handler

.macro MAKE_ISR_NOERR n
.type isr\n, @function
isr\n:
    cli
    pushl $0
    pushl $\n
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
.endm

.macro MAKE_ISR_ERR n
.type isr\n, @function
isr\n:
    cli
    pushl $\n
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
.endm

MAKE_ISR_NOERR 0
MAKE_ISR_NOERR 1
MAKE_ISR_NOERR 2
MAKE_ISR_NOERR 3
MAKE_ISR_NOERR 4
MAKE_ISR_NOERR 5
MAKE_ISR_NOERR 6
MAKE_ISR_NOERR 7
MAKE_ISR_ERR  8
MAKE_ISR_NOERR 9
MAKE_ISR_ERR  10
MAKE_ISR_ERR  11
MAKE_ISR_ERR  12
MAKE_ISR_ERR  13
MAKE_ISR_ERR  14
MAKE_ISR_NOERR 15
MAKE_ISR_NOERR 16
MAKE_ISR_NOERR 17
MAKE_ISR_NOERR 18
MAKE_ISR_NOERR 19
MAKE_ISR_NOERR 20
MAKE_ISR_NOERR 21
MAKE_ISR_NOERR 22
MAKE_ISR_NOERR 23
MAKE_ISR_NOERR 24
MAKE_ISR_NOERR 25
MAKE_ISR_NOERR 26
MAKE_ISR_NOERR 27
MAKE_ISR_NOERR 28
MAKE_ISR_NOERR 29
MAKE_ISR_NOERR 30
MAKE_ISR_NOERR 31

/* software interrupt stubs for inttest2 (0x22=34) and inttest (0x30=48) */
MAKE_ISR_NOERR 33   /* IRQ1 — keyboard interrupt */
MAKE_ISR_NOERR 34
MAKE_ISR_NOERR 48

/*
 * IRQ0 (PIT timer) — vector 0x20 = 32.
 * Uses the same save/restore frame as CPU exception stubs so isr_handler()
 * receives a registers_t* on the kernel stack.  The handler sends EOI and
 * may overwrite *regs to context-switch to a different process; iret then
 * jumps into whoever's state is in the frame.
 */
MAKE_ISR_NOERR 32
