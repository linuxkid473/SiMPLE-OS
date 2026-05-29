ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 1M;

    .stivale2hdr : {
        KEEP(*(.stivale2hdr))
    } :text

    .text : {
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    .data : {
        *(.data*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss*)
    } :data

    /* Exported so kernel.c can place kmalloc right after kernel image. */
    _kernel_end = ALIGN(4096);
}
