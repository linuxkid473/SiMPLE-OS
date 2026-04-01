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

GRUB_INSTALL := $(shell command -v i686-elf-grub-install 2>/dev/null || echo /opt/homebrew/Cellar/i686-elf-grub/2.12/i686-elf/sbin/i686-elf-grub-install)

SRC_C := $(wildcard kernel/src/*.c)
OBJ_C := $(patsubst kernel/src/%.c,$(BUILD_DIR)/%.o,$(SRC_C))
OBJ_ASM := $(BUILD_DIR)/boot.o $(BUILD_DIR)/gdt_asm.o $(BUILD_DIR)/isr.o $(BUILD_DIR)/isr_syscall.o
OBJS := $(OBJ_ASM) $(OBJ_C)

.PHONY: all image run clean user

all: image

user: user/hello.elf

# 🔥 FIXED USER BUILD (CRITICAL)
user/hello.elf: user/hello.c user/linker.ld
	$(CC) -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -O0 \
	-Wl,-T,user/linker.ld \
	-Wl,-N \
	-o $@ user/hello.c

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

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

image: $(KERNEL_ELF) user/hello.elf
	@set -e; \
	rm -f $(IMAGE); \
	truncate -s $(IMAGE_SIZE_MB)M $(IMAGE); \
	printf '\x80\x00\x02\x00\x06\xff\xff\xff\x00\x08\x00\x00\x00\xf8\x01\x00' | dd of=$(IMAGE) bs=1 seek=446 conv=notrunc >/dev/null 2>&1; \
	printf '\x55\xaa' | dd of=$(IMAGE) bs=1 seek=510 conv=notrunc >/dev/null 2>&1; \
	mkfs.fat -F 16 --offset $(PART_START_SECTOR) $(IMAGE) >/dev/null; \
	DISK="$$(hdiutil attach -nomount $(IMAGE) | awk 'index($$1, "/dev/disk") == 1 { print $$1; exit }')"; \
	RDISK="$${DISK/disk/rdisk}"; \
	MNT="$$(mktemp -d /tmp/simpleos-mnt.XXXX)"; \
	cleanup() { \
	  set +e; \
	  if mount | grep -q "on $$MNT "; then umount "$$MNT" >/dev/null 2>&1 || diskutil unmount force "$${DISK}s1" >/dev/null 2>&1; fi; \
	  if [ -n "$$MNT" ] && [ -d "$$MNT" ]; then rmdir "$$MNT" >/dev/null 2>&1; fi; \
	  if [ -n "$$DISK" ]; then hdiutil detach $$DISK >/dev/null 2>&1; fi; \
	}; \
	trap cleanup EXIT INT TERM; \
	mount -t msdos "$${DISK}s1" "$$MNT"; \
	mkdir -p "$$MNT/boot/grub"; \
	cp $(KERNEL_ELF) "$$MNT/boot/kernel.bin"; \
	cp grub/grub.cfg "$$MNT/boot/grub/grub.cfg"; \
	cp user/hello.elf "$$MNT/hello.elf"; \
	"$(GRUB_INSTALL)" --target=i386-pc --boot-directory="$$MNT/boot" --modules="part_msdos fat biosdisk multiboot normal configfile" --no-floppy "$$RDISK" >/dev/null; \
	sync; \
	cleanup; \
	trap - EXIT INT TERM

run: image
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE)

clean:
	rm -rf $(BUILD_DIR) $(IMAGE)
