.global isr_syscall
.extern isr_handler

.type isr_syscall, @function
isr_syscall:
    /*
     * No cli here.  This stub is invoked through a trap gate (IDT entry
     * 0x80, IDT_FLAG_TRAP), which preserves IF from the caller.  User code
     * running with interrupts enabled will enter the syscall handler with
     * IF=1, allowing PIT ticks to fire inside long-running syscall paths
     * such as SYS_SLEEP.  All other ISR stubs use interrupt gates (which
     * already clear IF at the hardware level), so their cli is harmless but
     * redundant.  Here it would permanently kill interrupts for the entire
     * duration of the syscall, causing hlt to deadlock.
     */
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
