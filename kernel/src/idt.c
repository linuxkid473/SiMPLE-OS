#include "idt.h"
#include "klog.h"
#include "panic.h"
#include "registers.h"
#include "serial.h"
#include "vga.h"

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

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
extern void isr_syscall(void);

static const char* exception_names[] = {
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
    idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[num].selector = 0x08;
    idt[num].zero = 0;
    idt[num].flags = flags;
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void) {
    for (uint32_t i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, 0, 0);
    }

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

    idt_set_gate(0x80, (uint32_t)isr_syscall,
        IDT_FLAG_PRESENT | IDT_FLAG_32BIT | IDT_FLAG_DPL3 | IDT_FLAG_TRAP);

    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idt_ptr.base = (uint32_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    klog("idt", "initialized with 32 exception handlers + syscall gate");
}

extern uint32_t exit_target;
extern uint32_t kernel_esp;
extern int process_exited;

static void syscall_handler(registers_t* regs) {
    switch (regs->eax) {
    case 1: {
        void sys_write(const char* buf, uint32_t len);
        sys_write((const char*)regs->ecx, regs->edx);
        break;
    }
    case 2:
        process_exited = 1;
        __asm__ volatile(
            "movl %0, %%esp\n\t"
            "jmp *%1\n\t"
            :
            : "r"(kernel_esp), "r"(exit_target)
            : "memory"
        );
        break;
    default:
        klog("syscall", "unknown syscall number");
        break;
    }
}

void isr_handler(registers_t* regs) {
    if (regs->int_no == 0x80) {
        syscall_handler(regs);
        return;
    }

    if (regs->int_no < 32) {
        const char* name = (regs->int_no < 32) ? exception_names[regs->int_no] : "Unknown";
        kernel_panic_regs(name, regs->int_no, regs->err_code,
                          regs->eip, regs->cs, regs->eflags);
        return;
    }

    serial_write(COM1, "[SIMPLE] isr: unhandled INT #");
    serial_write_dec(COM1, regs->int_no);
    serial_write(COM1, "\n");
}
