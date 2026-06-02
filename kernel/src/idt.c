/*
 * idt.c — IDT setup and central ISR dispatcher.
 *
 * Syscall ABI (Linux i386 compatible):
 *   eax = syscall number
 *   ebx = arg0
 *   ecx = arg1
 *   edx = arg2
 *   esi = arg3
 *   edi = arg4
 *   ebp = arg5
 */

#include "idt.h"
#include "elf.h"
#include "fd.h"
#include "gdt.h"
#include "keyboard.h"
#include "klog.h"
#include "panic.h"
#include "pic.h"
#include "pit.h"
#include "posix_errno.h"
#include "process.h"
#include "registers.h"
#include "serial.h"
#include "string.h"
#include "syscall.h"
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
extern void isr32(void);   /* IRQ0 — PIT timer,     vector 0x20 */
extern void isr33(void);   /* IRQ1 — keyboard IRQ,  vector 0x21 */
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

    idt_set_gate(0x20, (uint32_t)isr32, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(0x21, (uint32_t)isr33, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(0x22, (uint32_t)isr34, IDT_TYPE_INTERRUPT_GATE);
    idt_set_gate(0x30, (uint32_t)isr48, IDT_TYPE_INTERRUPT_GATE);

    /* int 0x80 syscall gate: DPL=3 so ring3 can invoke it */
    idt_set_gate(0x80, (uint32_t)isr_syscall,
        IDT_FLAG_PRESENT | IDT_FLAG_32BIT | IDT_FLAG_DPL3 | IDT_FLAG_TRAP);

    idt_ptr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idt_ptr.base  = (uint32_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));

    klog("idt", "initialized");
}

/* -------------------------------------------------------------------------
   User-process fault handler — [VM] diagnostics
   ---------------------------------------------------------------------- */
static void kill_user_process(registers_t *regs, const char *reason) {
    uint32_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    /* Full diagnostic dump to serial */
    serial_write(COM1, "[FAULT] ");
    serial_write(COM1, reason);
    serial_write(COM1, "\n");

    serial_write(COM1, "[FAULT]  PID=");
    if (current_proc >= 0) {
        serial_write_dec(COM1, (uint32_t)proc_table[current_proc].pid);
        serial_write(COM1, " (");
        serial_write(COM1, proc_table[current_proc].cwd);
        serial_write(COM1, ")");
    } else {
        serial_write(COM1, "?");
    }
    serial_write(COM1, " INT=");
    serial_write_dec(COM1, regs->int_no);
    serial_write(COM1, " ERR=0x");
    serial_write_hex(COM1, regs->err_code);
    serial_write(COM1, "\n");

    serial_write(COM1, "[FAULT]  EIP=0x");
    serial_write_hex(COM1, regs->eip);
    serial_write(COM1, " CS=0x");
    serial_write_hex(COM1, regs->cs);
    serial_write(COM1, " EFLAGS=0x");
    serial_write_hex(COM1, regs->eflags);
    serial_write(COM1, "\n");

    serial_write(COM1, "[FAULT]  ESP=0x");
    serial_write_hex(COM1, regs->useresp);
    serial_write(COM1, " EBP=0x");
    serial_write_hex(COM1, regs->ebp);
    serial_write(COM1, " CR2=0x");
    serial_write_hex(COM1, cr2);
    serial_write(COM1, "\n");

    serial_write(COM1, "[FAULT]  EAX=0x");
    serial_write_hex(COM1, regs->eax);
    serial_write(COM1, " EBX=0x");
    serial_write_hex(COM1, regs->ebx);
    serial_write(COM1, " ECX=0x");
    serial_write_hex(COM1, regs->ecx);
    serial_write(COM1, " EDX=0x");
    serial_write_hex(COM1, regs->edx);
    serial_write(COM1, "\n");

    if (regs->int_no == 14) {
        serial_write(COM1, "[FAULT]  #PF CR2=0x");
        serial_write_hex(COM1, cr2);
        serial_write(COM1, " (");
        serial_write(COM1, (regs->err_code & 1) ? "present"    : "not-present");
        serial_write(COM1, "|");
        serial_write(COM1, (regs->err_code & 2) ? "write"      : "read");
        serial_write(COM1, "|");
        serial_write(COM1, (regs->err_code & 4) ? "user-mode"  : "kernel-mode");
        if (regs->err_code & 8)  serial_write(COM1, "|reserved-bit");
        if (regs->err_code & 16) serial_write(COM1, "|instr-fetch");
        serial_write(COM1, ")\n");
    }

    /* For #UD (Invalid Opcode), dump 16 bytes at EIP */
    if (regs->int_no == 6) {
        uint32_t eip = regs->eip;
        if (eip >= 0x300000 && eip < 0x700000) {
            serial_write(COM1, "[FAULT]  #UD bytes at EIP:");
            const uint8_t *p = (const uint8_t *)eip;
            for (int i = 0; i < 16; i++) {
                serial_write(COM1, " ");
                /* print two hex digits */
                uint8_t b = p[i];
                const char *hex = "0123456789ABCDEF";
                char buf[3];
                buf[0] = hex[b >> 4];
                buf[1] = hex[b & 0xF];
                buf[2] = '\0';
                serial_write(COM1, buf);
            }
            serial_write(COM1, "\n");
        }
    }

    /* VGA display */
    vga_write("[FAULT] INT=");
    /* print int number (simple dec) */
    {
        uint32_t n = regs->int_no;
        if (n >= 10) vga_putc('0' + (char)(n / 10));
        vga_putc('0' + (char)(n % 10));
    }
    vga_write(" EIP=0x");
    {
        uint32_t n = regs->eip;
        const char *hex = "0123456789ABCDEF";
        for (int i = 28; i >= 0; i -= 4) vga_putc(hex[(n >> i) & 0xF]);
    }
    vga_write(" ");
    vga_write(reason);
    vga_putc('\n');

    /* Kill only the offending process; kernel keeps running */
    if (current_proc >= 0) {
        proc_exit(regs, W_SIGNALED(SIGSEGV));
    } else {
        proc_exit(regs, -1);
    }
}

/* -------------------------------------------------------------------------
   Forward declarations for syscall implementations
   ---------------------------------------------------------------------- */

/* From syscall.c — legacy implementations */
int32_t sys_write(const char *buf, uint32_t len);
int32_t sys_read(char *buf, uint32_t max_len);
int32_t sys_open(const char *path, uint32_t flags);
int32_t sys_close(int32_t fd);
int32_t sys_fread(int32_t fd, char *buf, uint32_t max_len);
int32_t sys_fwrite(int32_t fd, const char *buf, uint32_t len);
int32_t sys_seek(int32_t fd, int32_t offset, int32_t whence);
int32_t sys_exec(const char *path, registers_t *regs);
int32_t sys_sbrk(int32_t increment);
int32_t sys_getpid(void);
int32_t sys_getticks(void);
int32_t sys_stat(const char *path, void *out);
int32_t sys_readdir(const char *path, void *buf, uint32_t max_entries);
int32_t sys_rename(const char *old_path, const char *new_path);

/* New POSIX syscalls */
int32_t sys_linux_write(int32_t fd, const char *buf, uint32_t len);
int32_t sys_linux_read(int32_t fd, char *buf, uint32_t len, registers_t *regs);
int32_t sys_linux_open(const char *path, uint32_t flags, uint32_t mode);
int32_t sys_linux_close(int32_t fd);
int32_t sys_linux_lseek(int32_t fd, int32_t offset, int32_t whence);
int32_t sys_linux_unlink(const char *path);
int32_t sys_linux_rename(const char *old, const char *newp);
int32_t sys_linux_mkdir(const char *path, uint32_t mode);
int32_t sys_linux_rmdir(const char *path);
int32_t sys_linux_dup(int32_t fd);
int32_t sys_linux_dup2(int32_t oldfd, int32_t newfd);
int32_t sys_linux_pipe(int32_t *fds);
int32_t sys_linux_brk(uint32_t addr);
int32_t sys_linux_ioctl(int32_t fd, uint32_t req, uint32_t arg);
int32_t sys_linux_fcntl(int32_t fd, int32_t cmd, uint32_t arg);
int32_t sys_linux_setpgid(pid_t pid, pgid_t pgid);
int32_t sys_linux_setsid(void);
int32_t sys_linux_getppid(void);
int32_t sys_linux_getpgrp(void);
int32_t sys_linux_kill(pid_t pid, int sig);
int32_t sys_linux_sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int32_t sys_linux_sigprocmask(int how, const uint32_t *set, uint32_t *oset);
int32_t sys_linux_sigreturn(registers_t *regs);
int32_t sys_linux_sigsuspend(const uint32_t *mask);
int32_t sys_linux_sigpending(uint32_t *set);
int32_t sys_linux_rt_sigaction(int sig, const struct sigaction *act, struct sigaction *oact, uint32_t sz);
int32_t sys_linux_rt_sigprocmask(int how, const uint32_t *set, uint32_t *oset, uint32_t sz);
int32_t sys_linux_mmap2(uint32_t addr, uint32_t len, uint32_t prot, uint32_t flags, int32_t fd, uint32_t pgoff);
int32_t sys_linux_munmap(uint32_t addr, uint32_t len);
int32_t sys_linux_chdir(const char *path);
int32_t sys_linux_getcwd(char *buf, uint32_t len);
int32_t sys_linux_uname(void *buf);
int32_t sys_linux_nanosleep(const void *req, void *rem, registers_t *regs);
int32_t sys_linux_gettimeofday(void *tv, void *tz);
int32_t sys_linux_clock_gettime(int clk, void *tp);
int32_t sys_linux_getdents(int32_t fd, void *buf, uint32_t count);
int32_t sys_linux_getdents64(int32_t fd, void *buf, uint32_t count);
int32_t sys_linux_llseek(int32_t fd, uint32_t off_hi, uint32_t off_lo, uint64_t *result, uint32_t whence);
int32_t sys_linux_poll(void *fds, uint32_t nfds, int32_t timeout);
int32_t sys_linux_wait4(pid_t pid, int *status, int options, void *rusage, registers_t *regs);
int32_t sys_linux_execve(const char *path, char **argv, char **envp, registers_t *regs);
int32_t sys_linux_getuid32(void);
int32_t sys_linux_getgid32(void);
int32_t sys_linux_geteuid32(void);
int32_t sys_linux_getegid32(void);
int32_t sys_linux_prctl(uint32_t opt, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
int32_t sys_linux_umask(uint32_t mask);
int32_t sys_powerctl(int mode);
int32_t wm_syscall(uint32_t nr, uint32_t a, uint32_t b, uint32_t c);

/* -------------------------------------------------------------------------
   Syscall dispatcher — Linux i386 ABI
   ---------------------------------------------------------------------- */
static void syscall_handler(registers_t *regs) {
    uint32_t nr  = regs->eax;
    uint32_t a0  = regs->ebx;   /* arg0 */
    uint32_t a1  = regs->ecx;   /* arg1 */
    uint32_t a2  = regs->edx;   /* arg2 */
    uint32_t a3  = regs->esi;   /* arg3 */
    /* uint32_t a4  = regs->edi; */ /* arg4 (unused for now) */

    switch (nr) {
    /* ---- Linux syscalls ---- */
    case 1:  /* exit(code) */
        proc_exit(regs, W_EXITED((int)a0));
        return;

    case 2:  /* fork */
        regs->eax = (uint32_t)proc_fork(regs);
        break;

    case 3:  /* read(fd, buf, len) */
        regs->eax = (uint32_t)sys_linux_read((int32_t)a0, (char *)a1, a2, regs);
        break;

    case 4:  /* write(fd, buf, len) */
        regs->eax = (uint32_t)sys_linux_write((int32_t)a0, (const char *)a1, a2);
        break;

    case 5:  /* open(path, flags, mode) */
        regs->eax = (uint32_t)sys_linux_open((const char *)a0, a1, a2);
        break;

    case 6:  /* close(fd) */
        regs->eax = (uint32_t)sys_linux_close((int32_t)a0);
        break;

    case 7:  /* waitpid(pid, stat_addr, options) */
        regs->eax = (uint32_t)proc_waitpid((pid_t)a0, (int *)a1, (int)a2, regs);
        break;

    case 10: /* unlink(path) */
        regs->eax = (uint32_t)sys_linux_unlink((const char *)a0);
        break;

    case 11: /* execve(path, argv, envp) */
        regs->eax = (uint32_t)sys_linux_execve((const char *)a0, (char **)a1, (char **)a2, regs);
        break;

    case 12: /* chdir(path) */
        regs->eax = (uint32_t)sys_linux_chdir((const char *)a0);
        break;

    case 19: /* lseek(fd, offset, whence) */
        regs->eax = (uint32_t)sys_linux_lseek((int32_t)a0, (int32_t)a1, (int32_t)a2);
        break;

    case 20: /* getpid */
        regs->eax = (uint32_t)sys_getpid();
        break;

    case 37: /* kill(pid, sig) */
        regs->eax = (uint32_t)sys_linux_kill((pid_t)a0, (int)a1);
        break;

    case 38: /* rename(old, new) */
        regs->eax = (uint32_t)sys_linux_rename((const char *)a0, (const char *)a1);
        break;

    case 39: /* mkdir(path, mode) */
        regs->eax = (uint32_t)sys_linux_mkdir((const char *)a0, a1);
        break;

    case 40: /* rmdir(path) */
        regs->eax = (uint32_t)sys_linux_rmdir((const char *)a0);
        break;

    case 41: /* dup(fd) */
        regs->eax = (uint32_t)sys_linux_dup((int32_t)a0);
        break;

    case 42: /* pipe(fds[2]) */
        regs->eax = (uint32_t)sys_linux_pipe((int32_t *)a0);
        break;

    case 45: /* brk(addr) */
        regs->eax = (uint32_t)sys_linux_brk(a0);
        break;

    case 54: /* ioctl(fd, req, arg) */
        regs->eax = (uint32_t)sys_linux_ioctl((int32_t)a0, a1, a2);
        break;

    case 55: /* fcntl(fd, cmd, arg) */
        regs->eax = (uint32_t)sys_linux_fcntl((int32_t)a0, (int32_t)a1, a2);
        break;

    case 57: /* setpgid(pid, pgid) */
        regs->eax = (uint32_t)sys_linux_setpgid((pid_t)a0, (pgid_t)a1);
        break;

    case 63: /* dup2(oldfd, newfd) */
        regs->eax = (uint32_t)sys_linux_dup2((int32_t)a0, (int32_t)a1);
        break;

    case 64: /* getppid */
        regs->eax = (uint32_t)sys_linux_getppid();
        break;

    case 65: /* getpgrp */
        regs->eax = (uint32_t)sys_linux_getpgrp();
        break;

    case 66: /* setsid */
        regs->eax = (uint32_t)sys_linux_setsid();
        break;

    case 67: /* sigaction(sig, act, oact) */
        regs->eax = (uint32_t)sys_linux_sigaction((int)a0,
            (const struct sigaction *)a1, (struct sigaction *)a2);
        break;

    case 72: /* sigsuspend(mask) */
        regs->eax = (uint32_t)sys_linux_sigsuspend((const uint32_t *)a0);
        break;

    case 73: /* sigpending(set) */
        regs->eax = (uint32_t)sys_linux_sigpending((uint32_t *)a0);
        break;

    case 78: /* gettimeofday(tv, tz) */
        regs->eax = (uint32_t)sys_linux_gettimeofday((void *)a0, (void *)a1);
        break;

    case 90: /* mmap(addr,len,prot,flags,fd,off) — old mmap via ebx pointer */
        regs->eax = (uint32_t)sys_linux_mmap2(0, a1, a2, a3, -1, 0);
        break;

    case 91: /* munmap(addr, len) */
        regs->eax = (uint32_t)sys_linux_munmap(a0, a1);
        break;

    case 114: /* wait4(pid, stat, opts, rusage) */
        regs->eax = (uint32_t)sys_linux_wait4((pid_t)a0, (int *)a1, (int)a2, (void *)a3, regs);
        break;

    case 119: /* sigreturn */
        sys_linux_sigreturn(regs);
        return;  /* regs already modified */

    case 122: /* uname(buf) */
        regs->eax = (uint32_t)sys_linux_uname((void *)a0);
        break;

    case 126: /* sigprocmask(how, set, oset) */
        regs->eax = (uint32_t)sys_linux_sigprocmask((int)a0,
            (const uint32_t *)a1, (uint32_t *)a2);
        break;

    case 140: /* llseek(fd, hi, lo, result, whence) */
        regs->eax = (uint32_t)sys_linux_llseek((int32_t)a0, a1, a2,
            (uint64_t *)a3, regs->edi);
        break;

    case 141: /* getdents(fd, buf, count) */
        regs->eax = (uint32_t)sys_linux_getdents((int32_t)a0, (void *)a1, a2);
        break;

    case 162: /* nanosleep(req, rem) */
        regs->eax = (uint32_t)sys_linux_nanosleep((const void *)a0, (void *)a1, regs);
        break;

    case 168: /* poll(fds, nfds, timeout) */
        regs->eax = (uint32_t)sys_linux_poll((void *)a0, a1, (int32_t)a2);
        break;

    case 172: /* prctl(opt, ...) */
        regs->eax = (uint32_t)sys_linux_prctl(a0, a1, a2, a3, regs->edi);
        break;

    case 173: /* rt_sigaction(sig, act, oact, sz) */
        regs->eax = (uint32_t)sys_linux_rt_sigaction((int)a0,
            (const struct sigaction *)a1, (struct sigaction *)a2, a3);
        break;

    case 174: /* rt_sigprocmask(how, set, oset, sz) */
        regs->eax = (uint32_t)sys_linux_rt_sigprocmask((int)a0,
            (const uint32_t *)a1, (uint32_t *)a2, a3);
        break;

    case 175: /* rt_sigpending(set, sz) */
        regs->eax = (uint32_t)sys_linux_sigpending((uint32_t *)a0);
        break;

    case 177: /* rt_sigsuspend(mask, sz) */
        regs->eax = (uint32_t)sys_linux_sigsuspend((const uint32_t *)a0);
        break;

    case 183: /* getcwd(buf, len) */
        regs->eax = (uint32_t)sys_linux_getcwd((char *)a0, a1);
        break;

    case 186: /* gettid — same as getpid */
        regs->eax = (uint32_t)sys_getpid();
        break;

    case 192: /* mmap2(addr,len,prot,flags,fd,pgoff) */
        regs->eax = (uint32_t)sys_linux_mmap2(a0, a1, a2, a3,
            (int32_t)regs->edi, regs->ebp);
        break;

    case 199: /* getuid32 */
        regs->eax = (uint32_t)sys_linux_getuid32();
        break;

    case 200: /* getgid32 */
        regs->eax = (uint32_t)sys_linux_getgid32();
        break;

    case 201: /* geteuid32 */
        regs->eax = (uint32_t)sys_linux_geteuid32();
        break;

    case 202: /* getegid32 */
        regs->eax = (uint32_t)sys_linux_getegid32();
        break;

    case 220: /* getdents64(fd, buf, count) */
        regs->eax = (uint32_t)sys_linux_getdents64((int32_t)a0, (void *)a1, a2);
        break;

    case 252: /* exit_group(code) */
        proc_exit(regs, W_EXITED((int)a0));
        return;

    case 265: /* clock_gettime(clk, tp) */
        regs->eax = (uint32_t)sys_linux_clock_gettime((int)a0, (void *)a1);
        break;

    /* ---- SiMPLE-specific high-range syscalls ---- */
    case 400: /* getticks */
        regs->eax = (uint32_t)sys_getticks();
        break;

    case 401: case 402: case 403: case 404: case 405: case 406: case 407: {
        /* WM syscalls: mapped from 20-26 to 401-407 */
        uint32_t wm_nr = nr - 401 + 20;
        regs->eax = (uint32_t)wm_syscall(wm_nr, a0, a1, a2);
        break;
    }

    case 408: /* powerctl */
        regs->eax = (uint32_t)sys_powerctl((int)a0);
        break;

    case 409: /* stat_simple(path, out) */
        regs->eax = (uint32_t)sys_stat((const char *)a0, (void *)a1);
        break;

    case 410: /* readdir_simple(path, buf, max) */
        regs->eax = (uint32_t)sys_readdir((const char *)a0, (void *)a1, a2);
        break;

    /* ---- Legacy SiMPLE syscalls (kept for backward compat) ---- */
    /* Old syscall 1: write to stdout */
    /* Old syscall 2: exit — handled above as Linux exit */
    /* Old syscall 3: read from stdin */
    /* Old syscall 4: yield */
    case 500 + 4: /* yield via high alias */
        proc_yield(regs);
        regs->eax = 0;
        break;

    /* SYS_SBRK old-style (increment, not address) */
    case 500 + 15:
        regs->eax = (uint32_t)sys_sbrk((int32_t)a0);
        break;

    /* Old exec (path only) */
    case 500 + 10:
        regs->eax = (uint32_t)sys_exec((const char *)a0, regs);
        break;

    /* Old fread/fwrite via aliases */
    case 500 + 7:
        regs->eax = (uint32_t)sys_fread((int32_t)a0, (char *)a1, a2);
        break;
    case 500 + 8:
        regs->eax = (uint32_t)sys_fwrite((int32_t)a0, (const char *)a1, a2);
        break;
    case 500 + 9:
        regs->eax = (uint32_t)sys_seek((int32_t)a0, (int32_t)a1, (int32_t)a2);
        break;

    case 82: /* select — stub */
        regs->eax = (uint32_t)(-ENOSYS);
        break;

    default:
        klog_dec("syscall", "unknown", nr);
        regs->eax = (uint32_t)(-(int32_t)ENOSYS);
        break;
    }

    /* Deliver any pending signals when returning to user space */
    proc_deliver_signals(regs);
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
        pit_timer_tick(regs);
        /* Deliver signals after timer tick if returning to user */
        proc_deliver_signals(regs);
        return;
    }

    if (regs->int_no == 0x21) {
        /* IRQ1 — keyboard interrupt.  Reads one byte from PS/2, pushes
         * it to the scancode ring buffer, wakes kbd-blocked processes.
         * keyboard_irq_handler() sends EOI before returning. */
        keyboard_irq_handler();
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

    serial_write(COM1, "[SIMPLE] isr: vector 0x");
    serial_write_dec(COM1, regs->int_no);
    serial_write(COM1, "\n");
}
