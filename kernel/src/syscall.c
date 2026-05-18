/*
 * syscall.c — kernel-side syscall implementations.
 *
 * The actual dispatch happens in idt.c's static syscall_handler(), which
 * is called from isr_handler() on int 0x80.  This file provides the
 * individual syscall bodies that the dispatcher calls into.
 *
 * ABI (int 0x80):
 *   eax = syscall number
 *   ecx = arg0  (for SYS_WRITE: buffer pointer)
 *   edx = arg1  (for SYS_WRITE: byte count)
 *   return value in eax
 *
 * Syscall table:
 *   1  SYS_WRITE  — write bytes to the active terminal
 *   2  SYS_EXIT   — terminate program and return to shell
 *                   (implemented entirely in idt.c via kernel_esp / exit_target)
 */

#include "vga.h"
#include "types.h"

/*
 * SYS_WRITE (1): ecx = buf, edx = len
 *
 * Writes len bytes from buf to the active terminal.  vga_putc() routes to
 * whichever STerm session is currently live in the vga globals, which is
 * always the terminal that launched the program (exec_elf is called
 * synchronously from the shell with the correct session already loaded).
 */
void sys_write(const char* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        vga_putc(buf[i]);
}
