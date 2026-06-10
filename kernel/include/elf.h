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
 * The buffer is allocated via kmalloc(ELF_LOAD_BUF) from _kernel_end upwards.
 * 1 MB is sufficient for the largest ELF (term.elf ≈ 873 KB).
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

/* Maximum argv/envp entries accepted by the stack builders. */
#define POSIX_ARGV_MAX 32
#define POSIX_ENVP_MAX 16

/*
 * build_posix_stack_argv — build the Linux i386 initial stack from real
 * argument/environment vectors (kernel pointers, NULL-terminated arrays;
 * envp may be NULL).  Writes into the live virtual user space.
 * Returns the new user ESP.
 */
uint32_t build_posix_stack_argv(char *const argv[], char *const envp[]);

/* Same, but writes into the physical 1 MB block backing a spawned
 * process (virtual = USER_BASE + offset).  Returns the VIRTUAL user ESP. */
uint32_t build_posix_stack_phys_argv(uint8_t *phys_mem,
                                     char *const argv[], char *const envp[]);

/* exec_elf_spawn with explicit argv/envp (NULL-terminated kernel arrays). */
int exec_elf_spawn_argv(void *data, uint32_t data_len, int slot,
                        char *const argv[], char *const envp[]);

/*
 * build_posix_stack_phys — like build_posix_stack() but writes into a
 * physical memory block instead of the live virtual user space.
 *
 * phys_mem — kernel virtual pointer to the 1 MB physical block that backs
 *   0x300000–0x3FFFFF for the new process (PROC_SLOT_PHYS(slot)).
 * path     — argv[0] string.
 *
 * Returns the VIRTUAL user ESP the process should start with.
 */
uint32_t build_posix_stack_phys(uint8_t *phys_mem, const char *path);

/*
 * exec_elf_spawn — non-blocking ELF loader for concurrent processes.
 *
 * `data` must already point to the ELF binary in the target slot's physical
 * memory (i.e., data == (void*)PROC_SLOT_PHYS(slot)).  `data_len` is how
 * many bytes were read from disk.  `slot` is a free process slot (1–7,
 * state == PROC_DEAD) obtained from proc_find_spawn_slot().
 *
 * The function rearranges ELF segments within the physical block in-place,
 * plants stubs, builds the POSIX stack, and calls proc_spawn_user() to
 * make the new process PROC_RUNNABLE.
 *
 * Does NOT call kmalloc_reset() — safe when another process is live.
 * Returns the slot index on success, -1 on failure.
 */
int exec_elf_spawn(void *data, uint32_t data_len, int slot);

/*
 * exit_trampoline — iret lands here after SYS_EXIT patches the ISR frame.
 * Declared naked; restores all callee-saved registers (saved by launch_program
 * before the user stack switch) and kernel_esp, then `ret`s back into exec_elf.
 * Never called directly.
 */
void exit_trampoline(void);

#endif
