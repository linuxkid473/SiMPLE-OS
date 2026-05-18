/*
 * syscall.c — kernel-side syscall implementations.
 *
 * The actual dispatch happens in idt.c's static syscall_handler(), which
 * is called from isr_handler() on int 0x80.  This file provides the
 * individual syscall bodies that the dispatcher calls into.
 *
 * ABI (int 0x80):
 *   eax = syscall number
 *   ecx = arg0
 *   edx = arg1
 *   return value in eax (set via regs->eax in the dispatcher; popa restores it)
 *
 * Syscall table:
 *   1  SYS_WRITE  — write bytes to the active terminal
 *                   ecx = buf, edx = len
 *   2  SYS_EXIT   — terminate program and return to shell
 *                   (implemented entirely in idt.c via launch_program / exit_trampoline)
 *   3  SYS_READ   — blocking line input from the active terminal
 *                   ecx = buf, edx = max_len (including NUL)
 *                   returns: eax = bytes read (excluding NUL)
 */

#include "console.h"
#include "string.h"
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

/*
 * SYS_READ (3): ecx = buf, edx = max_len
 *
 * Blocks until the user presses Enter, collecting keyboard input into buf.
 * Echoes every character and handles backspace/cursor movement exactly as
 * the shell readline does, because it delegates to console_read_line().
 *
 * Alt+Arrow and Alt+Tab continue to work during the blocking read so the
 * user can still move windows and switch focus while an ELF program waits
 * for input.  If a non-terminal window is focused, keys are routed to that
 * window (calculator / SText) until the user switches back.
 *
 * The buffer is always NUL-terminated.  Returns the number of bytes stored
 * in buf, not counting the NUL.  If max_len == 0, returns 0 immediately.
 */
uint32_t sys_read(char* buf, uint32_t max_len) {
    if (max_len == 0) return 0;
    console_read_line(buf, max_len);
    return (uint32_t)strlen(buf);
}
