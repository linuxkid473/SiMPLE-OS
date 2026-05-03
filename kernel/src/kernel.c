#include "fat16.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "kmalloc.h"
#include "mouse.h"
#include "shell.h"
#include "vga.h"
#define _STIVALE2_SPLIT_64
#include "stivale2.h"

static struct stivale2_header_tag_framebuffer fb_tag = {
    .tag = {
        .identifier = STIVALE2_HEADER_TAG_FRAMEBUFFER_ID,
        .next = 0
    },
    .framebuffer_width = 800,
    .framebuffer_height = 600,
    .framebuffer_bpp = 32
};

void kernel_main(struct stivale2_struct *s2);

static __attribute__((aligned(16))) uint8_t kernel_stack[8192];

__attribute__((section(".stivale2hdr"), used, aligned(16)))
static struct stivale2_header stivale2_hdr = {
    .entry_point = (uint32_t)&kernel_main,
    .stack = (uint32_t)(kernel_stack + sizeof(kernel_stack)),
    .flags = 0,
    .tags = (uint32_t)&fb_tag
};

void *stivale2_get_tag(struct stivale2_struct *s2, uint64_t id) {
    struct stivale2_tag *current_tag = (void *)(uint32_t)s2->tags;
    for (;;) {
        if (!current_tag) {
            return 0;
        }
        if (current_tag->identifier == id) {
            return current_tag;
        }
        current_tag = (void *)(uint32_t)current_tag->next;
    }
}

void kernel_main(struct stivale2_struct *s2) {
    uint32_t memory_kb = 0;
    int memory_known = 0;
    int multiboot_ok = 1;

    if (s2) {
        struct stivale2_struct_tag_memmap *memmap_tag = stivale2_get_tag(s2, STIVALE2_STRUCT_TAG_MEMMAP_ID);
        if (memmap_tag) {
            uint64_t total_mem = 0;
            for (uint64_t i = 0; i < memmap_tag->entries; i++) {
                total_mem += memmap_tag->memmap[i].length;
            }
            memory_kb = (uint32_t)(total_mem / 1024);
            memory_known = 1;
        }

        struct stivale2_struct_tag_framebuffer *fb_tag = stivale2_get_tag(s2, STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);
        if (fb_tag) {
            fb_init((uint32_t*)(uint32_t)fb_tag->framebuffer_addr,
                    fb_tag->framebuffer_width,
                    fb_tag->framebuffer_height,
                    fb_tag->framebuffer_pitch);
        }
    }

    shell_set_boot_info(memory_kb, memory_known, multiboot_ok);

    vga_set_color(0x0F, 0x00);
    vga_clear();
    kmalloc_init(0x200000);
    vga_write_line("Welcome to SiMPLE OS");

    keyboard_init();
    mouse_init();
    gdt_init();
    vga_write_line("GDT OK");
    idt_init();
    vga_write_line("IDT OK");

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
