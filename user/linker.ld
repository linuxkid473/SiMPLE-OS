ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x00300000;  /* must match USER_BASE in kernel/src/elf.c */

    .flat : {
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
        *(COMMON)
    } :text
}
