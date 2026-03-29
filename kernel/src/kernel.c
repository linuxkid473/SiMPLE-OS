#include "fat16.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "vga.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
} multiboot_basic_info_t;

void kernel_main(unsigned int magic, unsigned int multiboot_info) {
    uint32_t memory_kb = 0;
    int memory_known = 0;
    int multiboot_ok = (magic == MULTIBOOT_BOOTLOADER_MAGIC);

    if (multiboot_ok && multiboot_info != 0) {
        multiboot_basic_info_t* mbi = (multiboot_basic_info_t*)multiboot_info;
        if (mbi->flags & 0x1) {
            memory_kb = mbi->mem_lower + mbi->mem_upper;
            memory_known = 1;
        }
    }

    shell_set_boot_info(memory_kb, memory_known, multiboot_ok);

    vga_set_color(0x0F, 0x00);
    vga_clear();
    vga_write_line("Welcome to SiMPLE OS");

    keyboard_init();
    mouse_init();

    fat16_fs_t fs;
    int fs_ready = (fat16_mount(&fs) == FAT16_OK);

    if (!fs_ready) {
        vga_write_line("Warning: FAT16 mount failed. FS commands disabled.");
    }

    shell_run(&fs, fs_ready);

    while (1) {
        __asm__ volatile("hlt");
    }
}
