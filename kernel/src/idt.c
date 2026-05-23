#include "idt.h"
#include "elf.h"
#include "fd.h"
#include "gdt.h"
#include "klog.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "process.h"
#include "registers.h"
#include "serial.h"
#include "string.h"
#include "vga.h"

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr32(void);   /* IRQ0 — PIT timer, vector 0x20 */
extern void isr34(void);
extern void isr48(void);
extern void isr_syscall(void);

static const char *exception_names[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

static void idt_set_gate(uint8_t num, uint32_t handler, uint8_t flags) {
    idt[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].selector    = SEG_KCODE;
    idt[num].zero        = 0;
    idt[num].flags       = flags;
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void) {
    for (uint32_t i = 0; i < IDT_ENTRIES; i++)
        idt_set_gate((uint8_t)i, 0, 0);

    idt_set_gate(0,  (uint32_t)isr0,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(1,  (uint32_t)isr1,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(2,  (uint32_t)isr2,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(3,  (uint32_t)isr3,  IDT_TYPE_TRAP_GATE);
    idt_set_gate(4,  (uint32_t)isr4,  IDT_TYPE_TRAP_GATE);
    idt_set_gate(5,  (uint32_t)isr5,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(6,  (uint32_t)isr6,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(7,  (uint32_t)isr7,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(8,  (uint32_t)isr8,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(9,  (uint32_t)isr9,  IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(10, (uint32_t)isr10, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(11, (uint32_t)isr11, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(12, (uint32_t)isr12, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(13, (uint32_t)isr13, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(14, (uint32_t)isr14, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(15, (uint32_t)isr15, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(16, (uint32_t)isr16, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(17, (uint32_t)isr17, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(18, (uint32_t)isr18, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(19, (uint32_t)isr19, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(20, (uint32_t)isr20, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(21, (uint32_t)isr21, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(22, (uint32_t)isr22, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(23, (uint32_t)isr23, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(24, (uint32_t)isr24, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(25, (uint32_t)isr25, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(26, (uint32_t)isr26, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(27, (uint32_t)isr27, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(28, (uint32_t)isr28, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(29, (uint32_t)isr29, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(30, (uint32_t)isr30, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(31, (uint32_t)isr31, IDT_TYPE_INTERRUPT_GATE);

    /*
     * IRQ0 (PIT timer) at vector 0x20.
     * Interrupt gate (IF=0 on entry): prevents nested timer IRQs while the
     * scheduler is running.  DPL=0 so ring3 cannot invoke it with `int 0x20`.
     */
    idt_set_gate(0x20, (uint32_t)isr32, IDT_TYPE_INTERRUPT_GATE);

    idt_set_gate(0x22, (uint32_t)isr34, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(0x30, (uint32_t)isr48, IDT_TYPE_INTERRUPT_GATE);

    /* int 0x80 syscall gate: DPL=3 so ring3 can invoke it */
    idt_set_gate(0x80, (uint32_t)isr_syscall,
        IDT_FLAG_PRESENT | IDT_FLAG_32BIT | IDT_FLAG_DPL3 | IDT_FLAG_TRAP);

    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idt_ptr.base  = (uint32_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    klog("idt", "initialized: 32 exception gates + ring3 syscall gate");
}

/* process_exited and exit_trampoline used in process.c (via proc_exit). */

/* -------------------------------------------------------------------------
   User-process fault handler
   Patches the iret frame so the ISR epilogue returns to exit_trampoline
   at ring0 instead of back into broken user code.
   ---------------------------------------------------------------------- */
static void kill_user_process(registers_t *regs, const char *reason) {
    serial_write(COM1, "[SIMPLE] user process killed: ");
    serial_write(COM1, reason);
    serial_write(COM1, "\n");
    serial_write(COM1, "[SIMPLE] INT=");   serial_write_dec(COM1, regs->int_no);
    serial_write(COM1, " ERR=");           serial_write_hex(COM1, regs->err_code);
    serial_write(COM1, " EIP=");           serial_write_hex(COM1, regs->eip);
    serial_write(COM1, " CS=");            serial_write_hex(COM1, regs->cs);
    serial_write(COM1, "\n");
    serial_write(COM1, "[SIMPLE] EAX=");   serial_write_hex(COM1, regs->eax);
    serial_write(COM1, " EBX=");           serial_write_hex(COM1, regs->ebx);
    serial_write(COM1, " ECX=");           serial_write_hex(COM1, regs->ecx);
    serial_write(COM1, " EDX=");           serial_write_hex(COM1, regs->edx);
    serial_write(COM1, "\n");
    serial_write(COM1, "[SIMPLE] ESP=");   serial_write_hex(COM1, regs->useresp);
    serial_write(COM1, " EBP=");           serial_write_hex(COM1, regs->ebp);
    serial_write(COM1, "\n");

    if (regs->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_write(COM1, "[SIMPLE] CR2=");
        serial_write_hex(COM1, cr2);
        serial_write(COM1, "\n");
        vga_write("User fault: ");
        vga_write(reason);
        vga_write("  CR2=");
        vga_write_hex(cr2);
        vga_putc('\n');
    } else {
        vga_write("User fault: ");
        vga_write_line(reason);
    }

    /*
     * Delegate to proc_exit which either schedules the next runnable process
     * or patches the iret frame to exit_trampoline (last process exiting).
     */
    proc_exit(regs, -1);
}

/* -------------------------------------------------------------------------
   Syscall dispatcher
   ---------------------------------------------------------------------- */
static void syscall_handler(registers_t *regs) {
    switch (regs->eax) {
    case 1: {
        int32_t sys_write(const char *buf, uint32_t len);
        regs->eax = (uint32_t)sys_write((const char *)regs->ecx, regs->edx);
        break;
    }
    case 2:
        /* SYS_EXIT: ecx = exit code */
        proc_exit(regs, (int)regs->ecx);
        break;
    case 3: {
        int32_t sys_read(char *buf, uint32_t max_len);
        regs->eax = (uint32_t)sys_read((char *)regs->ecx, regs->edx);
        break;
    }
    case 4:
        /* SYS_YIELD: cooperative yield to next runnable process */
        proc_yield(regs);
        regs->eax = 0;
        break;
    case 5: {
        int32_t sys_open(const char *path, uint32_t flags);
        regs->eax = (uint32_t)sys_open((const char *)regs->ecx, regs->edx);
        break;
    }
    case 6: {
        int32_t sys_close(int32_t fd);
        regs->eax = (uint32_t)sys_close((int32_t)regs->ecx);
        break;
    }
    case 7: {
        int32_t sys_fread(int32_t fd, char *buf, uint32_t max_len);
        regs->eax = (uint32_t)sys_fread((int32_t)regs->ecx,
                                         (char *)regs->edx,
                                         regs->ebx);
        break;
    }
    case 8: {
        int32_t sys_fwrite(int32_t fd, const char *buf, uint32_t len);
        regs->eax = (uint32_t)sys_fwrite((int32_t)regs->ecx,
                                          (const char *)regs->edx,
                                          regs->ebx);
        break;
    }
    case 9: {
        int32_t sys_seek(int32_t fd, int32_t offset, int32_t whence);
        regs->eax = (uint32_t)sys_seek((int32_t)regs->ecx,
                                        (int32_t)regs->edx,
                                        (int32_t)regs->ebx);
        break;
    }
    case 10: {
        /*
         * SYS_EXEC — ecx = path (user pointer).
         * On success: iret frame is patched; does not return to caller.
         * On failure: regs->eax gets -errno; iret returns normally.
         */
        int32_t sys_exec(const char *path, registers_t *regs);
        regs->eax = (uint32_t)sys_exec((const char *)regs->ecx, regs);
        break;
    }
    case 11:
        /*
         * SYS_FORK — duplicate current process.
         * Parent: regs->eax = child pid (>0).
         * Child:  saved_regs.eax = 0 (set inside proc_fork).
         */
        regs->eax = (uint32_t)proc_fork(regs);
        break;
    case 12:
        /*
         * SYS_WAIT — block until any child exits, then reap it.
         * Returns child's exit code, or -1 if no children exist.
         * When blocking: do_switch runs child; proc_exit() patches
         * saved_regs.eax with the exit code before switching back,
         * so the ISR epilogue delivers the correct value to ring3.
         */
        regs->eax = (uint32_t)proc_wait(regs);
        break;
    case 15: {
        /* SYS_SBRK — ecx = increment in bytes (>0 to grow heap).
         * Returns old break (start of newly usable memory), or -1 on error. */
        int32_t sys_sbrk(int32_t increment);
        regs->eax = (uint32_t)sys_sbrk((int32_t)regs->ecx);
        break;
    }
    default:
        klog_dec("syscall", "unknown syscall", regs->eax);
        regs->eax = (uint32_t)(-(int32_t)EINVAL);
        break;
    }
}

/* -------------------------------------------------------------------------
   Central ISR dispatcher
   ---------------------------------------------------------------------- */
void isr_handler(registers_t *regs) {
    if (regs->int_no == 0x80) {
        syscall_handler(regs);
        return;
    }

    if (regs->int_no == 0x20) {
        /* IRQ0: PIT timer tick — may preempt current ring3 process */
        pit_timer_tick(regs);
        return;
    }

    if (regs->int_no < 32) {
        const char *name = exception_names[regs->int_no];
        int from_user = (regs->cs & 3) == 3;

        if (from_user) {
            kill_user_process(regs, name);
        } else {
            kernel_panic_full(name, regs);
        }
        return;
    }

    vga_write("[INT] vector 0x");
    vga_write_hex(regs->int_no);
    vga_write_line(" handled, returning");
    serial_write(COM1, "[SIMPLE] isr: SW INT #");
    serial_write_dec(COM1, regs->int_no);
    serial_write(COM1, "\n");
}
