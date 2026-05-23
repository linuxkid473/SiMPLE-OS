CROSS ?= i686-elf
# Use the cross compiler if available, otherwise fall back to native gcc -m32
ifneq ($(shell command -v $(CROSS)-gcc 2>/dev/null),)
CC := $(CROSS)-gcc
else ifneq ($(shell command -v i686-linux-gnu-gcc 2>/dev/null),)
CC := i686-linux-gnu-gcc
else
CC := gcc
endif
AS := $(CC)

CFLAGS := -std=gnu99 -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -nostdlib -Wall -Wextra -O2 -Ikernel/include
ASFLAGS := -m32 -ffreestanding -nostdlib
LDFLAGS := -m32 -ffreestanding -nostdlib -no-pie -static -T kernel/linker.ld \
	-Wl,--no-dynamic-linker -Wl,--build-id=none

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/kernel.bin
IMAGE := simple.img

IMAGE_SIZE_MB := 64
PART_START_SECTOR := 2048
PART_SECTOR_COUNT := 129024

LIMINE_DIR := $(BUILD_DIR)/limine
LIMINE_SYS := $(LIMINE_DIR)/limine.sys
LIMINE_DEPLOY := $(LIMINE_DIR)/limine-deploy

SRC_C := $(wildcard kernel/src/*.c)
OBJ_C := $(patsubst kernel/src/%.c,$(BUILD_DIR)/%.o,$(SRC_C))
OBJ_ASM := $(BUILD_DIR)/boot.o $(BUILD_DIR)/gdt_asm.o $(BUILD_DIR)/isr.o $(BUILD_DIR)/isr_syscall.o
OBJS := $(OBJ_ASM) $(OBJ_C)

.PHONY: all image run clean user

all: image

user: user/hello.elf user/test.elf user/spam.elf user/systest.elf user/fwritetest.elf user/seektest.elf user/exectest.elf user/forktest.elf user/hog.elf user/multitest.elf user/forkwait.elf user/malloctest.elf

# User program build flags: no libc, no PIC, flat binary via linker.ld
USER_CC := $(CC) -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
	-Wl,-T,user/linker.ld -Wl,-N

user/hello.elf: user/hello.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/hello.c user/libc.c

user/test.elf: user/test.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/test.c user/libc.c

user/spam.elf: user/spam.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/spam.c user/libc.c

# systest is self-contained — no libc.c needed
user/systest.elf: user/systest.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/systest.c user/libc.c

user/fwritetest.elf: user/fwritetest.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/fwritetest.c user/libc.c

user/seektest.elf: user/seektest.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/seektest.c user/libc.c

user/exectest.elf: user/exectest.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/exectest.c user/libc.c

user/forktest.elf: user/forktest.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/forktest.c user/libc.c

user/hog.elf: user/hog.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/hog.c user/libc.c

user/multitest.elf: user/multitest.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/multitest.c user/libc.c

user/forkwait.elf: user/forkwait.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/forkwait.c user/libc.c

user/malloctest.elf: user/malloctest.c user/malloc.c user/libc.c user/linker.ld
	$(USER_CC) -o $@ user/malloctest.c user/malloc.c user/libc.c

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

image: $(KERNEL_ELF) user/hello.elf user/test.elf user/spam.elf user/systest.elf user/fwritetest.elf user/seektest.elf user/exectest.elf user/forktest.elf user/hog.elf user/multitest.elf user/forkwait.elf user/malloctest.elf $(LIMINE_SYS) $(LIMINE_DEPLOY) grub/limine.conf
	@set -e; \
	rm -f $(IMAGE); \
	truncate -s $(IMAGE_SIZE_MB)M $(IMAGE); \
	printf '\x80\x00\x02\x00\x06\xff\xff\xff\x00\x08\x00\x00\x00\xf8\x01\x00' | dd of=$(IMAGE) bs=1 seek=446 conv=notrunc >/dev/null 2>&1; \
	printf '\x55\xaa' | dd of=$(IMAGE) bs=1 seek=510 conv=notrunc >/dev/null 2>&1; \
	mkfs.fat -F 16 --offset $(PART_START_SECTOR) $(IMAGE) >/dev/null; \
	mcopy -i $(IMAGE)@@1048576 $(KERNEL_ELF) ::kernel.bin; \
	mcopy -i $(IMAGE)@@1048576 grub/limine.conf ::limine.cfg; \
	mcopy -i $(IMAGE)@@1048576 grub/limine.conf ::limine.conf; \
	mcopy -i $(IMAGE)@@1048576 $(LIMINE_SYS) ::limine-bios.sys; \
	mcopy -i $(IMAGE)@@1048576 $(LIMINE_SYS) ::limine.sys; \
	mcopy -i $(IMAGE)@@1048576 user/hello.elf ::hello.elf; \
	mcopy -i $(IMAGE)@@1048576 user/test.elf ::test.elf; \
	mcopy -i $(IMAGE)@@1048576 user/spam.elf ::spam.elf; \
	mcopy -i $(IMAGE)@@1048576 user/systest.elf ::systest.elf; \
	mcopy -i $(IMAGE)@@1048576 user/fwritetest.elf ::fwrite.elf; \
	mcopy -i $(IMAGE)@@1048576 user/seektest.elf ::seek.elf; \
	mcopy -i $(IMAGE)@@1048576 user/exectest.elf ::exec.elf; \
	mcopy -i $(IMAGE)@@1048576 user/forktest.elf ::fork.elf; \
	mcopy -i $(IMAGE)@@1048576 user/hog.elf ::hog.elf; \
	mcopy -i $(IMAGE)@@1048576 user/multitest.elf ::multi.elf; \
	mcopy -i $(IMAGE)@@1048576 user/forkwait.elf ::fwait.elf; \
	mcopy -i $(IMAGE)@@1048576 user/malloctest.elf ::malloc.elf; \
	parted -s $(IMAGE) mklabel msdos mkpart primary fat16 1MiB 100% set 1 boot on 2>/dev/null || true; \
        $(LIMINE_DEPLOY) $(IMAGE)

run: image
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE) -serial stdio -no-reboot -no-shutdown

clean:
	rm -rf $(BUILD_DIR) $(IMAGE)
