#include "power.h"
#include "io.h"

static void halt_forever(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

void poweroff(void) {
    __asm__ volatile("cli");

    /* Common QEMU/Bochs ACPI shutdown ports. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    halt_forever();
}

void reboot(void) {
    __asm__ volatile("cli");

    for (uint32_t spin = 0; spin < 0x10000; spin++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
        io_wait();
    }

    outb(0x64, 0xFE);
    halt_forever();
}
