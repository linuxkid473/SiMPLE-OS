void _start(void (*vga_putc)(char)) {
    vga_putc('O');
    vga_putc('K');
    vga_putc('!');
    vga_putc('\n');
}
