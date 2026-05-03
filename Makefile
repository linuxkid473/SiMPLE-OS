CROSS ?= i686-elf
CC := $(CROSS)-gcc
AS := $(CC)

CFLAGS := -std=gnu99 -m32 -ffreestanding -fno-stack-protector -fno-pic -nostdlib -Wall -Wextra -O2 -Ikernel/include
ASFLAGS := -m32 -ffreestanding -nostdlib
LDFLAGS := -m32 -ffreestanding -nostdlib -T kernel/linker.ld

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

user: user/hello.elf user/test.elf user/spam.elf

# 🔥 hello
user/hello.elf: user/hello.c user/linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
	-Wl,-T,user/linker.ld \
	-Wl,-N \
	-o $@ user/hello.c user/libc.c

# 🔥 test
user/test.elf: user/test.c user/linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
	-Wl,-T,user/linker.ld \
	-Wl,-N \
	-o $@ user/test.c user/libc.c

# 🔥 spam
user/spam.elf: user/spam.c user/linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
	-Wl,-T,user/linker.ld \
	-Wl,-N \
	-o $@ user/spam.c user/libc.c

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

image: $(KERNEL_ELF) user/hello.elf user/test.elf user/spam.elf $(LIMINE_SYS) $(LIMINE_DEPLOY) grub/limine.conf
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
	$(LIMINE_DEPLOY) $(IMAGE)

run: image
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(IMAGE)
