SiMPLE OS

<img width="1254" height="1254" alt="SiMPLE" src="https://github.com/user-attachments/assets/48f1adbb-e018-48a4-8e75-c9d2ffabcce9" />


A tiny graphical hobby operating system focused on simplicity, responsiveness, and systems experimentation.

⸻

About

SiMPLE OS is a custom x86 hobby operating system written from scratch in C and x86 assembly.

The project began as a minimal framebuffer experiment and gradually evolved into a small graphical desktop OS with:

* a custom window manager
* multiterminal support
* ELF executable loading
* a syscall interface
* interactive userspace-style programs
* mouse-driven GUI interaction
* a tiny desktop environment

The goal of SiMPLE OS is not POSIX compliance or Linux compatibility.

Instead, the project focuses on:

* clean architecture
* readability
* experimentation
* fast iteration
* low-level computer systems understanding

Everything currently runs in ring 0 by design to keep the system compact and easy to reason about.

⸻

Features

Desktop Environment

* Custom framebuffer desktop
* Multiwindow pseudo-WM
* Mouse cursor + window dragging
* Window focus + z-order
* App launcher menu
* Multiple instances of applications
* Close buttons
* Alt+Tab window switching
* Alt+Arrow keyboard window movement

Applications

STerm

A graphical terminal window with:

* independent terminal sessions
* shell integration
* multiline rendering
* command execution

SText

A lightweight text editor with:

* editable text buffer
* scrolling
* cursor movement
* multiline editing

Calculator

A clickable GUI calculator with:

* button grid
* integer arithmetic
* mouse interaction
* keyboard support

⸻

ELF Execution

SiMPLE OS supports loading and executing ELF programs directly from disk.

Current functionality:

* ELF loading
* executable relocation
* syscall interface
* synchronous task execution
* clean program return handling

Example:

run readtest.elf

⸻

Syscalls

Current syscall interface:

Syscall	Number	Description
SYS_WRITE	1	Write text to the active STerm
SYS_EXIT	2	Exit ELF program cleanly
SYS_READ	3	Blocking terminal input

Example userspace syscall:

__asm__ volatile(
    "int $0x80"
    :
    : "a"(1), "c"(buf), "d"(len)
    : "memory"
);

⸻

Window Manager

The WM is intentionally minimal.

Design goals:

* fixed-size arrays
* no dynamic allocation-heavy systems
* no compositing (yet)
* no retained widget frameworks
* tiny readable codebase

Current architecture:

* framebuffer renderer
* software cursor
* simple z-order rendering
* per-window application state

⸻

Filesystem

Current filesystem support:

* FAT16

The system currently uses 8.3 filenames for ELF programs and filesystem compatibility.

⸻

Building

Requirements

* x86_64 ELF cross compiler
* NASM
* QEMU
* GNU Make
* mtools

Example packages:

* x86_64-elf-gcc
* nasm
* qemu-system-x86_64
* mtools

⸻

Build

make

This produces:

simple.img

⸻

Running

QEMU

qemu-system-x86_64 \
  -m 512M \
  -drive format=raw,file=simple.img \
  -display cocoa,zoom-to-fit=on

⸻

Example Programs

readtest.elf

Interactive syscall test program:

=== readtest ===
Type something:
hello world
You typed: hello world

⸻

Architecture Goals

Planned future work includes:

* cooperative multitasking
* EXT2 support
* VFS layer
* compositing
* .kext loadable kernel extensions
* improved syscall layer
* IRQ subsystem cleanup
* timer-driven scheduling
* networking experiments

⸻

Philosophy

SiMPLE OS intentionally avoids overengineering.

The project values:

* simplicity
* experimentation
* visibility into the machine
* architectural clarity
* fun

It is heavily inspired by:

* TempleOS
* classic Macintosh systems
* BSD-style design philosophy
* hobbyist systems programming culture

⸻

Project Status

SiMPLE OS is an active experimental hobby OS and is not intended for production use.

Expect:

* bugs
* crashes
* architectural rewrites
* questionable design decisions
* sudden ambitious features at 3am

And that’s part of the fun.

⸻

License

See LICENSE.
