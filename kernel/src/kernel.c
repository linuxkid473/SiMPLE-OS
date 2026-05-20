#include "fat16.h"
#include "gdt.h"
#include "idt.h"
#include "keyboard.h"
#include "kmalloc.h"
#include "klog.h"
#include "mouse.h"
#include "paging.h"
#include "panic.h"
#include "process.h"
#include "serial.h"
#include "shell.h"
#include "vga.h"
#include "wm.h"
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

static const char *mmap_type_name(uint32_t type) {
    switch (type) {
    case STIVALE2_MMAP_USABLE: return "usable";
    case STIVALE2_MMAP_RESERVED: return "reserved";
    case STIVALE2_MMAP_ACPI_RECLAIMABLE: return "ACPI reclaimable";
    case STIVALE2_MMAP_ACPI_NVS: return "ACPI NVS";
    case STIVALE2_MMAP_BAD_MEMORY: return "bad memory";
    case STIVALE2_MMAP_BOOTLOADER_RECLAIMABLE: return "bootloader reclaimable";
    case STIVALE2_MMAP_KERNEL_AND_MODULES: return "kernel+modules";
    case STIVALE2_MMAP_FRAMEBUFFER: return "framebuffer";
    default: return "unknown";
    }
}

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
    serial_init(COM1);
    klog_boot("boot start");

    uint32_t memory_kb = 0;
    int memory_known = 0;
    int multiboot_ok = 1;
    uint32_t usable_mem_kb = 0;

    if (s2) {
        struct stivale2_struct_tag_memmap *memmap_tag =
            stivale2_get_tag(s2, STIVALE2_STRUCT_TAG_MEMMAP_ID);
        if (memmap_tag) {
            uint64_t total_mem = 0;
            uint64_t total_usable = 0;
            klog("memmap", "memory map from bootloader:");
            for (uint64_t i = 0; i < memmap_tag->entries; i++) {
                uint64_t base = memmap_tag->memmap[i].base;
                uint64_t length = memmap_tag->memmap[i].length;
                uint32_t type = memmap_tag->memmap[i].type;

                serial_write(COM1, "[SIMPLE] memmap: entry ");
                serial_write_dec(COM1, (uint32_t)i);
                serial_write(COM1, " base=");
                serial_write_hex(COM1, (uint32_t)(base >> 32));
                serial_write_hex(COM1, (uint32_t)(base & 0xFFFFFFFF));
                serial_write(COM1, " len=");
                serial_write_hex(COM1, (uint32_t)(length >> 32));
                serial_write_hex(COM1, (uint32_t)(length & 0xFFFFFFFF));
                serial_write(COM1, " type=");
                serial_write(COM1, mmap_type_name(type));
                serial_write(COM1, "\n");

                total_mem += length;
                if (type == STIVALE2_MMAP_USABLE) {
                    total_usable += length;
                }
            }
            memory_kb = (uint32_t)(total_mem / 1024);
            usable_mem_kb = (uint32_t)(total_usable / 1024);
            memory_known = 1;
            klog_dec("memmap", "total_memory_kb", memory_kb);
            klog_dec("memmap", "usable_memory_kb", usable_mem_kb);
        }

        struct stivale2_struct_tag_framebuffer *fb_tag =
            stivale2_get_tag(s2, STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID);
        if (fb_tag) {
            fb_init((uint32_t*)(uint32_t)fb_tag->framebuffer_addr,
                    fb_tag->framebuffer_width,
                    fb_tag->framebuffer_height,
                    fb_tag->framebuffer_pitch);
            wm_init((int)fb_tag->framebuffer_width,
                    (int)fb_tag->framebuffer_height);
            /* Tell the mouse driver the screen size before mouse_init()
             * so the cursor starts at screen centre and clamps correctly. */
            mouse_set_screen((int)fb_tag->framebuffer_width,
                             (int)fb_tag->framebuffer_height);
            klog("fb", "framebuffer initialized");
            klog_dec("fb", "width", fb_tag->framebuffer_width);
            klog_dec("fb", "height", fb_tag->framebuffer_height);
        }
    }

    klog_boot("serial initialized");

    vga_set_color(0x0F, 0x00);
    vga_clear();

    kmalloc_init(0x200000);
    klog_boot("memory manager initialized");
    klog_hex("kmalloc", "heap_start", 0x200000);
    klog_hex("kmalloc", "heap_size", KMALLOC_HEAP_SIZE);

    proc_init();
    klog_boot("process table initialized");

    keyboard_init();
    klog("keyboard", "initialized");

    mouse_init();
    klog("mouse", "initialized");

    gdt_init();
    klog_boot("GDT initialized");

    paging_init();
    klog_boot("paging initialized (ring3 protection active)");

    idt_init();
    klog_boot("IDT initialized");

    vga_write_line("Welcome to SiMPLE OS");

    fat16_fs_t fs;
    int fs_ready = (fat16_mount(&fs) == FAT16_OK);

    if (fs_ready) {
        klog_boot("filesystem mounted");
        klog_hex("fat16", "partition_lba", fs.partition_lba);
        klog_dec("fat16", "bytes_per_sector", fs.bytes_per_sector);
        klog_dec("fat16", "sectors_per_cluster", fs.sectors_per_cluster);
        klog_dec("fat16", "total_sectors", fs.total_sectors);
    } else {
        klog_fail("fat16", "mount failed, FS commands disabled");
        vga_write_line("Warning: FAT16 mount failed. FS commands disabled.");
    }

    shell_set_boot_info(memory_kb, memory_known, multiboot_ok);

    klog_boot("entering kernel main");
    klog_boot("scheduler online");

    shell_run(&fs, fs_ready);

    while (1) {
        __asm__ volatile("hlt");
    }
}
