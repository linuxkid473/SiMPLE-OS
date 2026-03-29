# SiMPLE OS

SiMPLE OS is a minimal bootable operating system for `x86_64` QEMU that:

- boots via **GRUB** (Multiboot)
- runs a freestanding **32-bit C kernel**
- uses **VGA text mode** + **keyboard input**
- provides an interactive shell (`SiMPLE>`) with filesystem commands
- implements a basic **FAT16** driver (read/write dirs and files)
- includes a simple line-based text editor (`edit <file>`)

## Implemented Commands

- `help`
- `clear`
- `echo <text>`
- `ls`
- `cd <dir>` (`..`, `.`, and `/` supported)
- `open <file>`
- `mkdir <dir>`
- `touch <file>`
- `edit <file>`
- `rm <name>` (files and empty directories)

## Project Structure

```text
SiMPLE/
├── Makefile
├── README.md
├── grub/
│   └── grub.cfg
└── kernel/
    ├── boot.s
    ├── linker.ld
    ├── include/
    │   ├── ata.h
    │   ├── console.h
    │   ├── editor.h
    │   ├── fat16.h
    │   ├── io.h
    │   ├── keyboard.h
    │   ├── shell.h
    │   ├── string.h
    │   ├── types.h
    │   └── vga.h
    └── src/
        ├── ata.c
        ├── console.c
        ├── editor.c
        ├── fat16.c
        ├── kernel.c
        ├── keyboard.c
        ├── shell.c
        ├── string.c
        └── vga.c
```

## Build

From the project root:

```bash
make image
```

This builds:

- `build/kernel.bin` (Multiboot kernel)
- `simple.img` (raw disk image with MBR + FAT16 + GRUB + kernel)

## Run

```bash
qemu-system-x86_64 -drive format=raw,file=simple.img
```

On boot you should see:

```text
Welcome to SiMPLE OS
SiMPLE>
```

## Notes

- FAT16 implementation is short-name based (8.3 names, uppercase on disk).
- Keyboard handling uses polling of PS/2 scancodes.
- Disk access uses ATA PIO (LBA28) for sector reads/writes.
- Shell input supports in-line cursor editing and command history.
- Editor supports cursor movement, in-place insert/delete, and `:w`, `:q`, `:wq`.

## Key Components

### Boot path

1. BIOS starts GRUB from MBR-installed boot code.
2. GRUB loads `/boot/kernel.bin` using Multiboot.
3. Kernel starts at `_start` in `kernel/boot.s` and calls `kernel_main`.

### FAT16

- Mounts first FAT16 partition from MBR.
- Parses BPB and computes FAT/root/data regions.
- Supports:
  - directory enumeration
  - directory creation (`mkdir`)
  - file creation (`touch`)
  - file read (`open`)
  - file overwrite/write (`edit` save path)
  - file and empty-directory delete (`rm`)
  - folder navigation (`cd`)

### Shell + editor

- Shell loop handles command parsing and dispatch.
- `edit` opens a simple append-based editor:
  - type lines to append
  - `:w` save
  - `:q` quit without saving
  - `:wq` save and quit
