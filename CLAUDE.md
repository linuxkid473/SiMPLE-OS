# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Commands

### Building
```bash
make image     # Build kernel and create bootable disk image
make run       # Build and run in QEMU
make clean     # Remove build artifacts and disk image
make user      # Build user programs (hello.elf)
```

### Development Workflow
1. Edit source files in `kernel/src/` or `user/`
2. Run `make image` to rebuild the disk image
3. Run `make run` to test in QEMU
4. Use shell commands within SiMPLE OS to test functionality

### User Program Development
User programs are built as freestanding ELF binaries:
```bash
# Build the example hello program
make user

# To create a new user program:
# 1. Add .c file to user/ directory
# 2. Add appropriate linker script or use user/linker.ld
# 3. Add build rule to Makefile or compile manually:
#    $(CC) -ffreestanding -nostdlib -Wl,-T,user/linker.ld -o user/prog.elf user/prog.c
```

## Code Architecture

### Kernel Structure
- `kernel/boot.s` - Assembly entry point, sets up protected mode
- `kernel/src/kernel.c` - Main kernel entry (`kernel_main`), initialization loop
- Hardware drivers:
  - `vga.c` - VGA text mode display (0xB8000)
  - `keyboard.c` - PS/2 keyboard polling
  - `ata.c` - ATA PIO disk access (LBA28)
  - `fat16.c` - FAT16 filesystem driver
  - `console.c` - Terminal abstraction layer
  - `shell.c` - Command REPL with history and editing
  - `editor.c` - Simple line-based text editor
  - `string.c` - Custom string implementations
  - `kmalloc.c` - Simple heap allocator
  - `elf.c` - ELF program loader

### Key Data Flow
1. **Boot**: BIOS → GRUB → loads `boot/kernel.bin` → jumps to `_start` in `boot.s`
2. **Initialization**: `kernel_main()` sets up hardware, memory, filesystem
3. **Shell Loop**: `shell_run()` provides `SiMPLE >` prompt, processes commands
4. **Program Execution**: `run <command>` triggers:
   - FAT16 file read
   - ELF parsing and relocation via `elf_load()`
   - Stack setup and entry point call
   - Return to shell after program exit

### Memory Model
- Single address space: kernel and user programs share all memory
- No virtual memory or memory protection
- `kmalloc()` provides simple heap allocation from fixed memory pool
- User programs loaded via ELF loader into heap memory (not original p_vaddr)
- Stack allocated per-program in heap memory

### Constraints & Requirements
- **No libc**: Cannot use printf, malloc, strcpy, etc. Use kernel equivalents
- **No standard runtime**: All code must be freestanding (-ffreestanding -nostdlib)
- **32-bit only**: Targets i686-elf toolchain
- **Shared memory**: User programs can corrupt kernel if they write wild pointers
- **Kernel-mediated I/O**: All display output must go through `print()` or `vga_putc()`
- **Direct hardware access**: Drivers talk directly to hardware ports (no HAL)

## Programming Guidelines

### Kernel Development
- Use `#include <kernel/include/*.h>` for kernel headers
- Hardware initialization happens in `kernel_main()` before shell start
- Keep functions small and focused; avoid complex abstractions
- Error handling: return error codes or halt on fatal errors (no exceptions)

### User Program Development
- Programs must be freestanding ELF binaries
- Entry point: `_start()` (not main, unless you set up crt0)
- Available kernel functions: `print()`, `vga_putc()`, and any exported symbols
- No access to libc functions; implement needed functionality yourself
- Programs execute in kernel address space - be careful with pointers

### String Handling
- Use functions from `string.h`/`string.c` (strlen, strcpy, strcmp, etc.)
- Kernel provides basic string operations; no dynamic string allocation
- String buffers must be pre-allocated (stack or heap via kmalloc)

## Debugging Approach
1. **Boot failures**: Check GRUB configuration and Multiboot header
2. **Display issues**: Verify VGA initialization and cursor positioning
3. **Keyboard problems**: Confirm PS/2 controller communication and scancode translation
4. **Disk/filesystem issues**: Validate FAT16 BPB parsing and sector reads/writes
5. **Program loading**: Check ELF loader logs and relocation calculations
6. **Triple faults**: Often indicate stack corruption or invalid segment descriptors

## Testing Philosophy
- No automated test suite; verification is manual via QEMU
- Test incrementally: boot → shell → filesystem → program loading
- Keep changes small and verify each works before proceeding
- Use SiMPLE OS shell commands to inspect system state (ls, cat, etc.)

## File Conventions
- Header files in `kernel/include/`
- Source files in `kernel/src/` or `user/`
- Build artifacts in `build/`
- Disk image: `simple.img` in project root
- Linker scripts: `.ld` files in respective directories