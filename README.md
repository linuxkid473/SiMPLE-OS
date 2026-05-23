# SiMPLE OS

A from-scratch 32-bit hobby operating system written in C and x86 assembly.

![SiMPLE OS screenshot](https://github.com/user-attachments/assets/48f1adbb-e018-48a4-8e75-c9d2ffabcce9)

---

## What it is

SiMPLE OS boots via the Stivale2 protocol on Limine, runs on i686, and provides a graphical desktop with a real kernel/userspace split, preemptive multitasking, and a syscall interface.

It started as a framebuffer experiment and grew into a small but complete OS kernel with working process isolation, ELF loading, FAT16 storage, and a GUI desktop.

---

## Architecture

| Component | Details |
|---|---|
| Boot | Limine → Stivale2 → `kernel_main()` |
| Target | i686, 32-bit, freestanding C + x86 asm |
| Display | 800×600 32bpp framebuffer |
| Memory | Bump allocator, per-process paging |
| Scheduling | Preemptive round-robin, 100Hz PIT, 100ms timeslice |
| Processes | Up to 4 concurrent, ring 3 isolation |
| Filesystem | FAT16 over ATA PIO (LBA28) |
| Syscalls | `int 0x80`, 12 calls |

---

## Syscalls

ABI: `eax` = number, `ecx` = arg0, `edx` = arg1, `ebx` = arg2, return in `eax`.

| # | Name | Args | Description |
|---|---|---|---|
| 1 | `SYS_WRITE` | buf, len | Write to VGA terminal |
| 2 | `SYS_EXIT` | code | Terminate process |
| 3 | `SYS_READ` | buf, max_len | Blocking keyboard input |
| 4 | `SYS_YIELD` | — | Cooperative yield |
| 5 | `SYS_OPEN` | path, flags | Open/create file |
| 6 | `SYS_CLOSE` | fd | Release file descriptor |
| 7 | `SYS_FREAD` | fd, buf, len | Read from file |
| 8 | `SYS_FWRITE` | fd, buf, len | Write to file |
| 9 | `SYS_SEEK` | fd, offset, whence | Seek in file |
| 10 | `SYS_EXEC` | path | Replace process with ELF |
| 11 | `SYS_FORK` | — | Fork current process |
| 12 | `SYS_WAIT` | — | Wait for child to exit |

---

## Desktop

- Framebuffer window manager
- Mouse cursor + window dragging, focus, z-order
- Alt+Tab switching, Alt+Arrow movement
- App launcher

**Built-in apps:**
- **STerm** — graphical terminal with independent shell sessions
- **SText** — text editor with scrolling and cursor movement
- **Calculator** — GUI calculator with mouse + keyboard input

---

## Process Model

- Up to 4 concurrent processes
- Ring 3 userspace, ring 0 kernel
- Per-process page directories (user memory isolated)
- Preemptive scheduler via PIT at 100Hz
- `proc_fork()` duplicates page directory + register state
- Process states: `RUNNING`, `RUNNABLE`, `BLOCKED`, `ZOMBIE`, `DEAD`

---

## Building

**Requirements:**
- `i686-elf-gcc` (cross compiler)
- `nasm`
- `qemu-system-x86_64`
- `mtools`
- `make`

```bash
make
```

Produces `simple.img`.

---

## Running

```bash
qemu-system-x86_64 \
  -m 512M \
  -drive format=raw,file=simple.img \
  -display sdl
```

---

## Shell Commands

```
help, clear, echo, ls, cd, open, edit, touch, mkdir, rm, cp, mv
run <file.elf>   — execute a userspace ELF binary
ps               — show process table
poweroff / reboot
```

---

## User Programs

Freestanding ELF binaries in `user/` using `int 0x80`:

| Program | Tests |
|---|---|
| `hello.c` | Basic write syscall |
| `forkwait.c` | SYS_FORK + SYS_WAIT |
| `forktest.c` | Fork behavior |
| `exectest.c` | SYS_EXEC |
| `fwritetest.c` | File I/O |
| `seektest.c` | SYS_SEEK |
| `systest.c` | Syscall coverage |

---

## Roadmap

- [ ] `SYS_SBRK` + userspace `malloc`
- [ ] VFS layer
- [ ] EXT2 support
- [ ] Networking experiments
- [ ] Loadable kernel modules
- [ ] Compositing

---

## Philosophy

SiMPLE OS is not trying to be Linux or POSIX-compatible. It exists to be readable, hackable, and fun. Inspired by TempleOS, classic Macintosh internals, and BSD design philosophy.

Expect bugs, architectural rewrites, and ambitious features implemented at 3am.

---

## License

See [LICENSE](LICENSE).
```
