#include "elf.h"
#include "vga.h"
#include "string.h"
#include "types.h"

#define USER_BASE  0x100000
#define USER_STACK 0x200000

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

    uint32_t base = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            base = USER_BASE - phdr[i].p_vaddr;
            break;
        }
    }

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        uint8_t* dest = (uint8_t*)(phdr[i].p_vaddr + base);
        uint8_t* src  = (uint8_t*)data + phdr[i].p_offset;

        for (uint32_t j = 0; j < phdr[i].p_filesz; j++) {
            dest[j] = src[j];
        }

        for (uint32_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
            dest[j] = 0;
        }
    }

    uint32_t entry = ehdr->e_entry + base;
    uint32_t stack_top = USER_STACK;

    __asm__ volatile(
        "movl %0, %%esp\n\t"
        "push %1\n\t"
        "call *%2"
        :
        : "r"(stack_top), "r"((void*)vga_putc), "r"((void*)entry)
        : "memory"
    );

    vga_write_line("Returned from program");
    return 0;
}
