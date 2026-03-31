#include "elf.h"
#include "vga.h"
#include "kmalloc.h"
#include "string.h"

int elf_validate(void* data) {
    uint8_t* e = (uint8_t*)data;

    if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F')
        return -1;

    if (e[4] != 1)
        return -1;

    if (((Elf32_Ehdr*)data)->e_type != ET_EXEC)
        return -1;

    return 0;
}

int exec_elf(void* data) {
    if (elf_validate(data) != 0) {
        vga_write_line("Invalid ELF");
        return -1;
    }

    Elf32_Ehdr* ehdr = (Elf32_Ehdr*)data;
    Elf32_Phdr* phdr = (Elf32_Phdr*)((uint8_t*)data + ehdr->e_phoff);

    uint32_t min_vaddr = 0xFFFFFFFF;
    uint32_t max_vaddr = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        if (phdr[i].p_vaddr < min_vaddr)
            min_vaddr = phdr[i].p_vaddr;

        uint32_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (end > max_vaddr)
            max_vaddr = end;
    }

    uint32_t size = max_vaddr - min_vaddr;

    void* base = kmalloc(size);
    if (!base) {
        vga_write_line("Failed to allocate memory");
        return -1;
    }

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        void* dest = (uint8_t*)base + (phdr[i].p_vaddr - min_vaddr);
        void* src = (uint8_t*)data + phdr[i].p_offset;

        memcpy(dest, src, phdr[i].p_filesz);

        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            memset((uint8_t*)dest + phdr[i].p_filesz, 0,
                   phdr[i].p_memsz - phdr[i].p_filesz);
        }
    }

    void* stack = kmalloc(16384);
    uint32_t stack_top = (uint32_t)stack + 16384;

    void* entry = (uint8_t*)base + (ehdr->e_entry - min_vaddr);

    __asm__ volatile(
        "movl %0, %%esp\n\t"
        "push %1\n\t"
        "call *%2"
        :
        : "r"(stack_top), "r"((void*)vga_putc), "r"(entry)
        : "memory"
    );

    vga_write_line("Returned from program");
    return 0;
}
