# =============================================================================
# SiMPLE OS — top-level build system
# =============================================================================

# ---------------------------------------------------------------------------
# Toolchain detection (prefer i686-elf cross-compiler, fall back to native)
# ---------------------------------------------------------------------------
CROSS ?= i686-elf
ifneq ($(shell command -v $(CROSS)-gcc 2>/dev/null),)
CC := $(CROSS)-gcc
else ifneq ($(shell command -v i686-linux-gnu-gcc 2>/dev/null),)
CC := i686-linux-gnu-gcc
else
CC := gcc
endif
AS := $(CC)

# ---------------------------------------------------------------------------
# Kernel build flags
# ---------------------------------------------------------------------------
CFLAGS  := -std=gnu99 -m32 -ffreestanding -fno-stack-protector \
           -fno-pic -fno-pie -nostdlib -Wall -Wextra -O2 -Ikernel/include
ASFLAGS := -m32 -ffreestanding -nostdlib
LDFLAGS := -m32 -ffreestanding -nostdlib -no-pie -static \
           -T kernel/linker.ld -Wl,--no-dynamic-linker -Wl,--build-id=none

# ---------------------------------------------------------------------------
# Image / partition layout
# ---------------------------------------------------------------------------
BUILD_DIR        := build
KERNEL_ELF       := $(BUILD_DIR)/kernel.bin
IMAGE            := simple.img
IMAGE_SIZE_MB    := 64
PART_START_SECTOR := 2048
PART_SECTOR_COUNT := 129024

# ---------------------------------------------------------------------------
# Limine bootloader
# ---------------------------------------------------------------------------
LIMINE_DIR    := $(BUILD_DIR)/limine
LIMINE_SYS    := $(LIMINE_DIR)/limine.sys
LIMINE_DEPLOY := $(LIMINE_DIR)/limine-deploy

# ---------------------------------------------------------------------------
# Kernel sources (all .c files in kernel/src/ are compiled automatically)
# ---------------------------------------------------------------------------
SRC_C   := $(wildcard kernel/src/*.c)
OBJ_C   := $(patsubst kernel/src/%.c,$(BUILD_DIR)/%.o,$(SRC_C))
OBJ_ASM := $(BUILD_DIR)/boot.o $(BUILD_DIR)/gdt_asm.o \
           $(BUILD_DIR)/isr.o $(BUILD_DIR)/isr_syscall.o
OBJS    := $(OBJ_ASM) $(OBJ_C)

# ---------------------------------------------------------------------------
# User-space build infrastructure
# ---------------------------------------------------------------------------

# gcc's own freestanding headers (stdarg.h, stddef.h, stdint.h, etc.)
# We use -nostdinc to stop the compiler searching /usr/include (which has
# the host libc) and add back only the compiler-internal directory via
# -isystem.  Our own POSIX-subset headers live in user/include/ and take
# precedence via the leading -I.
GCC_INTERNAL_INCLUDE := $(shell $(CC) -m32 -print-file-name=include 2>/dev/null)

# Flags shared by all user ELF targets:
#   -ffreestanding   : no hosted-env assumptions
#   -nostdlib        : don't link against host libc/crt
#   -nostdinc        : don't search host /usr/include
#   -isystem ...     : but DO allow gcc's own freestanding headers
#                      (stdarg.h, stddef.h, stdint.h, float.h, limits.h …)
#   -I user/include  : our POSIX-subset headers (stdio.h, unistd.h, etc.)
#   -I user          : for legacy programs that do #include "wm.h" etc.
USER_CFLAGS := -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
               -nostdinc -isystem $(GCC_INTERNAL_INCLUDE) \
               -I user/include -I user

# Linker flags for user ELFs: flat single-segment layout, entry = _start
USER_LDFLAGS := -Wl,-T,user/linker.ld -Wl,-N

# Convenience macro — compile + link in one step (no separate .o files)
USER_CC := $(CC) $(USER_CFLAGS) $(USER_LDFLAGS)

# ---------------------------------------------------------------------------
# Userland POSIX runtime — linked into every modern user ELF.
#
# WHY crt0.c?
#   The kernel launches a user ELF by iret-ing to its _start symbol with
#   a POSIX-style initial stack:
#       [esp+0]  = argc
#       [esp+4]  = argv[0]  (pointer to program-name string)
#       [esp+8]  = NULL     (argv terminator)
#       [esp+12] = NULL     (envp terminator — kernel passes no env)
#       [esp+16] = AT_NULL  (auxv)
#   crt0.c provides the naked _start that reads those values from the
#   stack, stores envp in 'environ', then calls main(argc, argv, envp).
#   On return it calls exit(eax).
#
#   Programs that don't link crt0.c must define their own _start and
#   call exit() themselves before returning.
#
# WHY were libc symbols previously unresolved?
#   Old programs were linked with just "program.c user/libc.c".  When
#   they started using printf/fopen (from stdio.c) or malloc/free (from
#   malloc.c) those symbols were not in the link, hence "undefined
#   reference to printf" etc.  USER_RUNTIME bundles every piece so a
#   single rule produces a complete, self-contained ELF.
# ---------------------------------------------------------------------------
# USER_RUNTIME source list — see comment block above for rationale.
# Components:  crt0 (_start→main bridge)
#              libc (syscall wrappers)
#              stdio (FILE*/printf)
#              stdlib (malloc extras, qsort, atoi, string helpers)
#              malloc (bump allocator via sbrk)
#              env (getenv/setenv/environ)
#              setjmp (setjmp/longjmp, assembly)
#              dirent (opendir/readdir/closedir)
USER_RUNTIME := \
    user/crt0.c \
    user/libc.c \
    user/stdio.c \
    user/stdlib.c \
    user/malloc.c \
    user/env.c \
    user/setjmp.S \
    user/dirent.c

# Runtime dependency list for make rules (no comments — make can't parse them)
USER_RUNTIME_DEPS := \
    user/crt0.c \
    user/libc.c \
    user/stdio.c \
    user/stdlib.c \
    user/malloc.c \
    user/env.c \
    user/setjmp.S \
    user/dirent.c \
    user/linker.ld

# Runtime sources passed to the compiler (same list, no linker.ld)
USER_RUNTIME_SRCS := \
    user/crt0.c \
    user/libc.c \
    user/stdio.c \
    user/stdlib.c \
    user/malloc.c \
    user/env.c \
    user/setjmp.S \
    user/dirent.c

# ---------------------------------------------------------------------------
# Legacy runtime — for programs that define _start() themselves.
# These pre-date the POSIX layer and call exit() directly; they must NOT
# link crt0.c (that would produce two _start definitions).
# ---------------------------------------------------------------------------
LEGACY_RUNTIME_DEPS := user/libc.c user/linker.ld
LEGACY_RUNTIME_SRCS := user/libc.c

# ---------------------------------------------------------------------------
# Phony targets
# ---------------------------------------------------------------------------
.PHONY: all image run clean user

all: image

# 'make user' — build only the user ELFs (handy during userland development)
user: \
    user/hello.elf   \
    user/posixtest.elf \
    user/test.elf    \
    user/spam.elf    \
    user/systest.elf \
    user/fwritetest.elf \
    user/seektest.elf \
    user/exectest.elf \
    user/forktest.elf \
    user/hog.elf     \
    user/multitest.elf \
    user/forkwait.elf \
    user/malloctest.elf \
    user/wmtest.elf  \
    user/calc.elf    \
    user/term.elf    \
    user/desktop.elf \
    user/paint.elf   \
    user/snake.elf   \
    user/ticker_a.elf \
    user/ticker_b.elf \
    user/ticker_c.elf \
    user/isoA.elf    \
    user/isoB.elf    \
    user/vmfork.elf  \
    user/crash.elf   \
    user/smkhelp.elf \
    user/posixsmoke.elf

# =============================================================================
# MODERN POSIX user programs — define main(), link against USER_RUNTIME
# =============================================================================

# hello.elf (was hello.c, now the POSIX stress-test / posixstress program)
user/hello.elf: user/hello.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/hello.c $(USER_RUNTIME_SRCS)

# posixtest.elf — simpler smoke-test
user/posixtest.elf: user/posixtest.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/posixtest.c $(USER_RUNTIME_SRCS)

# ticker_a/b/c.elf — Phase 2 scheduler verification (print A/B/C in a loop)
user/ticker_a.elf: user/ticker_a.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/ticker_a.c $(USER_RUNTIME_SRCS)

user/ticker_b.elf: user/ticker_b.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/ticker_b.c $(USER_RUNTIME_SRCS)

user/ticker_c.elf: user/ticker_c.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/ticker_c.c $(USER_RUNTIME_SRCS)

# =============================================================================
# LEGACY user programs — define _start(), link against LEGACY_RUNTIME only
# These programs do NOT need (and must NOT link) crt0.c.
# =============================================================================

user/test.elf: user/test.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/test.c $(LEGACY_RUNTIME_SRCS)

user/spam.elf: user/spam.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/spam.c $(LEGACY_RUNTIME_SRCS)

user/systest.elf: user/systest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/systest.c $(LEGACY_RUNTIME_SRCS)

user/fwritetest.elf: user/fwritetest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/fwritetest.c $(LEGACY_RUNTIME_SRCS)

user/seektest.elf: user/seektest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/seektest.c $(LEGACY_RUNTIME_SRCS)

user/exectest.elf: user/exectest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/exectest.c $(LEGACY_RUNTIME_SRCS)

user/forktest.elf: user/forktest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/forktest.c $(LEGACY_RUNTIME_SRCS)

user/hog.elf: user/hog.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/hog.c $(LEGACY_RUNTIME_SRCS)

user/multitest.elf: user/multitest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/multitest.c $(LEGACY_RUNTIME_SRCS)

user/forkwait.elf: user/forkwait.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/forkwait.c $(LEGACY_RUNTIME_SRCS)

user/malloctest.elf: user/malloctest.c user/malloc.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/malloctest.c user/malloc.c $(LEGACY_RUNTIME_SRCS)

user/wmtest.elf: user/wmtest.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/wmtest.c $(LEGACY_RUNTIME_SRCS)

user/calc.elf: user/calc.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/calc.c $(LEGACY_RUNTIME_SRCS)

user/term.elf: user/term.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/term.c $(LEGACY_RUNTIME_SRCS)

user/desktop.elf: user/desktop.c user/dirent.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/desktop.c user/dirent.c $(LEGACY_RUNTIME_SRCS)

user/paint.elf: user/paint.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/paint.c $(LEGACY_RUNTIME_SRCS)

user/snake.elf: user/snake.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/snake.c $(LEGACY_RUNTIME_SRCS)

# ---------------------------------------------------------------------------
# VM isolation / validation test programs
# ---------------------------------------------------------------------------

user/isoA.elf: user/isoA.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/isoA.c $(LEGACY_RUNTIME_SRCS)

user/isoB.elf: user/isoB.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/isoB.c $(LEGACY_RUNTIME_SRCS)

user/vmfork.elf: user/vmfork.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/vmfork.c $(LEGACY_RUNTIME_SRCS)

user/crash.elf: user/crash.c $(LEGACY_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/crash.c $(LEGACY_RUNTIME_SRCS)

# smkhelp.elf — execve helper: exits with code 42
user/smkhelp.elf: user/smkhelp.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/smkhelp.c $(USER_RUNTIME_SRCS)

# posixsmoke.elf — comprehensive POSIX syscall smoke test
user/posixsmoke.elf: user/posixsmoke.c $(USER_RUNTIME_DEPS)
	$(USER_CC) -o $@ user/posixsmoke.c $(USER_RUNTIME_SRCS)

# =============================================================================
# Kernel build rules
# =============================================================================

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LIMINE_SYS): | $(BUILD_DIR)
	mkdir -p $(LIMINE_DIR)
	curl -sLo $(LIMINE_SYS) https://raw.githubusercontent.com/limine-bootloader/limine/v3.20221230.0-binary/limine.sys

$(LIMINE_DEPLOY): | $(BUILD_DIR)
	mkdir -p $(LIMINE_DIR)
	curl -sLo $(LIMINE_DIR)/limine-deploy.c https://raw.githubusercontent.com/limine-bootloader/limine/v3.20221230.0-binary/limine-deploy.c
	curl -sLo $(LIMINE_DIR)/limine-hdd.h https://raw.githubusercontent.com/limine-bootloader/limine/v3.20221230.0-binary/limine-hdd.h
	cc -O2 $(LIMINE_DIR)/limine-deploy.c -o $(LIMINE_DEPLOY)

$(BUILD_DIR)/boot.o: kernel/boot.s | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt_asm.o: kernel/src/gdt_asm.s | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/isr.o: kernel/src/isr.s | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/isr_syscall.o: kernel/src/isr_syscall.s | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: kernel/src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

# =============================================================================
# Disk image — kernel + all user ELFs copied to FAT16 partition
# =============================================================================
image: \
    $(KERNEL_ELF) \
    user/hello.elf \
    user/posixtest.elf \
    user/test.elf \
    user/spam.elf \
    user/systest.elf \
    user/fwritetest.elf \
    user/seektest.elf \
    user/exectest.elf \
    user/forktest.elf \
    user/hog.elf \
    user/multitest.elf \
    user/forkwait.elf \
    user/malloctest.elf \
    user/wmtest.elf \
    user/calc.elf \
    user/term.elf \
    user/desktop.elf \
    user/paint.elf \
    user/snake.elf \
    user/isoA.elf \
    user/isoB.elf \
    user/vmfork.elf \
    user/crash.elf \
    user/smkhelp.elf \
    user/posixsmoke.elf \
    $(LIMINE_SYS) $(LIMINE_DEPLOY) grub/limine.conf
	@set -e; \
	rm -f $(IMAGE); \
	truncate -s $(IMAGE_SIZE_MB)M $(IMAGE); \
	printf '\x80\x00\x02\x00\x06\xff\xff\xff\x00\x08\x00\x00\x00\xf8\x01\x00' \
	    | dd of=$(IMAGE) bs=1 seek=446 conv=notrunc >/dev/null 2>&1; \
	printf '\x55\xaa' | dd of=$(IMAGE) bs=1 seek=510 conv=notrunc >/dev/null 2>&1; \
	mkfs.fat -F 16 --offset $(PART_START_SECTOR) $(IMAGE) >/dev/null; \
	mcopy -i $(IMAGE)@@1048576 $(KERNEL_ELF)         ::kernel.bin; \
	mcopy -i $(IMAGE)@@1048576 grub/limine.conf       ::limine.cfg; \
	mcopy -i $(IMAGE)@@1048576 grub/limine.conf       ::limine.conf; \
	mcopy -i $(IMAGE)@@1048576 $(LIMINE_SYS)          ::limine-bios.sys; \
	mcopy -i $(IMAGE)@@1048576 $(LIMINE_SYS)          ::limine.sys; \
	mcopy -i $(IMAGE)@@1048576 user/hello.elf         ::hello.elf; \
	mcopy -i $(IMAGE)@@1048576 user/posixtest.elf     ::posix.elf; \
	mcopy -i $(IMAGE)@@1048576 user/test.elf          ::test.elf; \
	mcopy -i $(IMAGE)@@1048576 user/spam.elf          ::spam.elf; \
	mcopy -i $(IMAGE)@@1048576 user/systest.elf       ::systest.elf; \
	mcopy -i $(IMAGE)@@1048576 user/fwritetest.elf    ::fwrite.elf; \
	mcopy -i $(IMAGE)@@1048576 user/seektest.elf      ::seek.elf; \
	mcopy -i $(IMAGE)@@1048576 user/exectest.elf      ::exec.elf; \
	mcopy -i $(IMAGE)@@1048576 user/forktest.elf      ::fork.elf; \
	mcopy -i $(IMAGE)@@1048576 user/hog.elf           ::hog.elf; \
	mcopy -i $(IMAGE)@@1048576 user/multitest.elf     ::multi.elf; \
	mcopy -i $(IMAGE)@@1048576 user/forkwait.elf      ::fwait.elf; \
	mcopy -i $(IMAGE)@@1048576 user/malloctest.elf    ::malloc.elf; \
	mcopy -i $(IMAGE)@@1048576 user/wmtest.elf        ::wmtest.elf; \
	mcopy -i $(IMAGE)@@1048576 user/calc.elf          ::calc.elf; \
	mcopy -i $(IMAGE)@@1048576 user/term.elf          ::term.elf; \
	mcopy -i $(IMAGE)@@1048576 user/desktop.elf       ::desktop.elf; \
	mcopy -i $(IMAGE)@@1048576 user/paint.elf         ::paint.elf; \
	mcopy -i $(IMAGE)@@1048576 user/snake.elf         ::snake.elf; \
	mcopy -i $(IMAGE)@@1048576 user/ticker_a.elf      ::ticker_a.elf; \
	mcopy -i $(IMAGE)@@1048576 user/ticker_b.elf      ::ticker_b.elf; \
	mcopy -i $(IMAGE)@@1048576 user/ticker_c.elf      ::ticker_c.elf; \
	mcopy -i $(IMAGE)@@1048576 user/isoA.elf          ::isoA.elf; \
	mcopy -i $(IMAGE)@@1048576 user/isoB.elf          ::isoB.elf; \
	mcopy -i $(IMAGE)@@1048576 user/vmfork.elf        ::vmfork.elf; \
	mcopy -i $(IMAGE)@@1048576 user/crash.elf         ::crash.elf; \
	mcopy -i $(IMAGE)@@1048576 user/smkhelp.elf       ::smkhelp.elf; \
	mcopy -i $(IMAGE)@@1048576 user/posixsmoke.elf    ::smoke.elf; \
	parted -s $(IMAGE) mklabel msdos mkpart primary fat16 1MiB 100% \
	    set 1 boot on 2>/dev/null || true; \
	$(LIMINE_DEPLOY) $(IMAGE)

run: image
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE) -serial stdio \
	    -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR) $(IMAGE)
