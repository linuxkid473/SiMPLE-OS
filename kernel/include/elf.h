#ifndef SIMPLE_ELF_H
#define SIMPLE_ELF_H

#include "types.h"

#define PT_LOAD 1
#define ET_EXEC 2

/* User address space bounds — must match user/linker.ld and paging.c */
#define USER_BASE   0x300000U
#define USER_STACK  0x400000U

/*
 * ELF_LOAD_BUF — size of the kernel-heap buffer used to stage an ELF binary
 * before copying its PT_LOAD segments into user space.
 *
 * Largest binary on disk: term.elf ≈ 873 KB.
 * The buffer is allocated via kmalloc(ELF_LOAD_BUF) starting at 0x200000;
 * 1 MB brings heap_ptr to exactly 0x300000 = USER_BASE (safe, non-overlapping).
 *
 * sys_exec (fork+exec path) does NOT use this buffer — it reads directly
 * into user space and does an in-place copy, avoiding any size limit.
 */
#define ELF_LOAD_BUF  0x100000U   /* 1 MB */

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

int      elf_validate(void* data);
int      exec_elf(void* data);
uint32_t build_posix_stack(const char *path);

/*
 * exit_trampoline — iret lands here after SYS_EXIT patches the ISR frame.
 * Declared naked; restores all callee-saved registers (saved by launch_program
 * before the user stack switch) and kernel_esp, then `ret`s back into exec_elf.
 * Never called directly.
 */
void exit_trampoline(void);

#endif
