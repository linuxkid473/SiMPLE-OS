# Porting Vim to SiMPLE OS

## 1. Strategy decision

Goal: a working modal `vim filename.txt` in the native SiMPLE terminal with the
highest possible compatibility and the fewest hacks.

Candidates evaluated against SiMPLE's hard constraints — a **1 MB user address
space** (code + data + stack share 0x300000–0x3FFFFF; static ET_EXEC only), a
**3 MB process heap**, a hand-rolled libc subset, a FAT16 (8.3-name) root
filesystem and a kernel-rendered ANSI terminal:

| Option | Verdict | Reason |
| ------ | ------- | ------ |
| Upstream Vim 9 | **Not feasible** | vim "normal" build is ~3 MB of text+data; needs terminfo, locale, iconv, full stdio, select(), wide libc. Cannot fit the 1 MB user image even before libc work. |
| Vim `--with-features=tiny` | **Not feasible** | vim.tiny is still ~1.9 MB statically; same address-space blocker, plus ~100 missing libc symbols (terminfo, signal stack, `select`, locale). Would require a kernel VM redesign first. |
| nvi | Rejected | Requires curses *and* Berkeley DB; each is a bigger port than the editor itself. |
| Traditional vi (ex-vi / Heirloom) | Rejected | Requires termcap and substantial K&R-era surgery; larger and less vim-like than the chosen option. |
| **neatvi** (vendored at `user/vim/`) | **Chosen** | ~9 k LOC, ISC-licensed, explicitly vim-modeled (modal normal/insert/visual, ex commands, regex search, undo/redo, registers, marks, multiple windows, syntax highlighting). No curses/termcap — speaks plain VT100/ANSI. Uses only POSIX calls SiMPLE already has or that are reasonable to add. ~200 KB binary. |

This satisfies the fallback ladder: tiny Vim and nvi are ruled out by hard
memory/dependency constraints, and neatvi is the most vim-compatible of the
"traditional vi" class — strictly more compatible than a custom editor.

## 2. Compatibility matrix

| Feature | Vim/neatvi requires | SiMPLE has (before port) | Action needed |
| ------- | ------------------- | ------------------------ | ------------- |
| Raw keyboard input | `tcsetattr(~ICANON, ~ECHO, ~ISIG)`, byte-at-a-time `read(0)` | termios ioctls stored but raw path drops arrow/Del keys; no escape-sequence synthesis | **Kernel**: real raw-mode input queue; arrows/Del/Home/End/PgUp/PgDn → VT100 sequences; ICRNL honored; ECHO honored |
| `poll(0, POLLIN, -1)` | Blocking poll on stdin | poll never blocks, TTY always "ready" | **Kernel**: true TTY readiness + blocking with PIT-based timeout |
| Esc key | byte 27 from keyboard | keymap already maps scancode 0x01 → 27 | none |
| Cursor addressing | `ESC[r;cH`, `ESC[nG` | CUP/CUU/CUD/CUF/CUB yes; CHA (`G`) missing | **Kernel**: add CHA |
| Erase | `ESC[K`, `ESC[2J` | yes | none |
| SGR colors | `ESC[m`, 30–37/40–47, bold, reverse | yes (8+8 colors) | 256-color codes (38;5;N) tolerated, mapped approximately |
| Scroll region | `ESC[r`, `ESC[t;br` (DECSTBM) | missing | **Kernel**: implement DECSTBM + region-aware scrolling |
| Insert/delete line | `ESC[nL`, `ESC[nM` | missing | **Kernel**: implement via terminal cell buffer |
| Visible cursor | block cursor at insertion point | none in framebuffer mode | **Kernel**: software inverse-video cursor + `ESC[?25h/l` |
| Window size | `ioctl(TIOCGWINSZ)` | yes, but hardcoded 80×25 | **Kernel**: report real framebuffer grid |
| `argc/argv` | `vim filename.txt` | execve ignores argv; shell passes no args | **Kernel**: argv/envp on initial stack; shell tokenizes and passes args; shell runs `name.elf` for unknown command `name` |
| Foreground process | exclusive keyboard | ring-0 shell keeps reading keys after spawn | **Kernel**: shell waits for child exit, restores termios/screen after |
| `malloc/free` (heavy) | working `free`, sane heap | bump allocator, `free` no-op, `realloc` corrupts | **libc**: real free-list allocator with split/coalesce |
| `stat()` (mtime) | POSIX `struct stat` | SiMPLE-specific 409 stat (size/is_dir/exists) | **libc**: POSIX wrapper over 409; mtime=0 (FAT16 mtime not read) |
| ctype.h | isdigit/isalpha/tolower/… | missing | **libc**: new header + table-free implementations |
| termios.h / poll.h / sys/ioctl.h | standard headers | functions exist in libc.c but no headers | **libc**: new headers matching kernel ABI |
| `getenv` | LINES/COLUMNS/EXINIT (optional) | env layer exists; kernel passes empty envp | works (returns NULL); optional |
| `snprintf` family | `%d %s %c %x`, width | yes | none |
| `qsort`, `strtol`, `strchr`… | yes | yes | none |
| File I/O | open/read/write/lseek/close, O_CREAT/O_TRUNC | yes (FAT16, 8.3 names) | none (8.3 limit documented) |
| `access(R_OK)` | existence check | yes (mode ignored) | none |
| fork/exec/pipes for `:!cmd` | `/bin/sh`, execvp, sockets | no shell, no sockets | **editor patch**: `cmd_pipe`/`cmd_unix` stubbed (no `:!` filters); execvp/socket libc stubs for link |
| Job control (`^Z`) | SIGSTOP/SIGCONT + shell job control | signals exist; shell has no job control | **editor patch**: `term_suspend` disabled |
| SIGWINCH | resize detection | never generated | not needed (fixed-size display) |

## 3. What was changed (summary — see git log for detail)

### New kernel functionality
- Raw-mode TTY input queue (`kernel/src/tty.c`): translates key events to a
  byte stream, synthesizes VT100 escape sequences for special keys, honors
  ICRNL/ECHO/ISIG/VMIN.
- Non-blocking keyboard event API (`keyboard_poll_event`) refactored out of the
  blocking reader so the TTY layer and `poll()` can pump without sleeping.
- `read(0)` raw path rewritten; `poll()` blocks with timeout for TTY fds.
- VT100 upgrades in `kernel/src/vga.c`: DECSTBM scroll regions, region-aware
  scrolling, insert/delete line (CSI L/M), CHA (CSI G), DSR cursor report,
  software block cursor with CSI ?25h/l.
- `execve` argv/envp support; POSIX initial stack built from real argument
  vectors (both spawn paths).
- Shell: arguments after the command are passed through; unknown commands fall
  back to `<name>.elf`; the shell blocks until the foreground program exits and
  then restores termios + screen state.
- `TIOCGWINSZ` reports the real framebuffer character grid.

### New libc functionality
- Real heap allocator (free list, split/coalesce, correct `realloc`).
- `ctype.h`, `termios.h`, `poll.h`, `sys/ioctl.h`, `time.h` headers.
- POSIX `stat()/fstat()` wrappers, `execvp`, socket stubs, `R_OK` etc.

### Editor patches (kept minimal)
- `cmd.c`: `:!` pipelines and the UNIX-socket channel stubbed out (no shell on
  SiMPLE).
- `term.c`: `^Z` suspend disabled (no job control).
- `conf.h`: colors limited to the 16-color VGA palette.

## 4. Usage

```
SiMPLE ~ > vim sample.txt        # or: run vim.elf sample.txt
```

Verified working in QEMU: open/edit/save existing files, create new files,
hjkl + arrow keys + Home/End/PgUp/PgDn/Del, insert/normal/ex modes, `x`,
`dd`, `o`, `u` (undo), `yy`/`p`, `/` search, `:w`, `:q`, `:q!`, `:wq`,
counts, full-screen scrolling and repeated runs in one boot without
degradation.  The shell waits for the editor and restores terminal state
when it exits.

## 5. Remaining limitations

- **No `:!cmd` / filters / `%!`** — SiMPLE has no `/bin/sh`; `cmd_pipe`
  is stubbed (shows a message).
- **No `^Z` suspend** — the shell has no job control; `term_suspend` is a
  no-op.
- **8.3 filenames only** (FAT16, root + subdirectories): `sample.txt` is
  fine, `verylongname.txt` is not.
- **No mtime-based "file changed" detection** — FAT16 timestamps are not
  read; `stat()` reports a constant mtime of 1 for existing files (this
  also encodes "file exists" for vi's overwrite check).
- **Arrow keys in insert mode** exit insert mode and then move (vintage-vi
  behaviour) rather than moving within insert mode.
- **256-colour SGR (`38;5;N`) is approximated** by the 16-colour VGA
  palette; unknown SGR params are ignored.
- **Search does not wrap** past EOF (upstream neatvi behaviour).
- File size per buffer is bounded by the 3 MB process heap and the
  kernel's 64 KB FAT16 read staging buffer per `read()` call.
- `poll()` on pipes does not block-wait on data (TTY waiting is fully
  supported); irrelevant for the editor since `cmd_pipe` is stubbed.
- No SIGWINCH (the display never resizes).
