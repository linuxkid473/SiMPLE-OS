ENTRY(_start)

SECTIONS
{
    . = 0x00100000;

    .flat : {
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
}
