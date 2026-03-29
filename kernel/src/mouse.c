#include "mouse.h"
#include "io.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t*)0xB8000)

static int mouse_enabled = 0;
static uint8_t packet[3];
static uint8_t packet_index = 0;

static int pointer_x = VGA_WIDTH / 2;
static int pointer_y = VGA_HEIGHT / 2;
static int pointer_drawn = 0;
static int pointer_prev_x = VGA_WIDTH / 2;
static int pointer_prev_y = VGA_HEIGHT / 2;
static uint16_t pointer_saved_original = 0;
static uint16_t pointer_saved_drawn = 0;

static int wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            return 1;
        }
    }
    return 0;
}

static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) {
            return 1;
        }
    }
    return 0;
}

static int mouse_write(uint8_t value) {
    if (!wait_input_clear()) {
        return 0;
    }
    outb(0x64, 0xD4);
    if (!wait_input_clear()) {
        return 0;
    }
    outb(0x60, value);
    return 1;
}

static uint16_t invert_cell(uint16_t cell) {
    uint8_t ch = (uint8_t)(cell & 0x00FF);
    uint8_t attr = (uint8_t)((cell >> 8) & 0x00FF);
    uint8_t fg = (uint8_t)(attr & 0x0F);
    uint8_t bg = (uint8_t)((attr >> 4) & 0x0F);
    uint8_t inv = (uint8_t)((fg << 4) | bg);

    if (inv == attr) {
        inv ^= 0x77;
    }

    return (uint16_t)ch | ((uint16_t)inv << 8);
}

static void pointer_erase_if_needed(void) {
    if (!pointer_drawn) {
        return;
    }

    uint16_t index = (uint16_t)(pointer_prev_y * VGA_WIDTH + pointer_prev_x);
    if (VGA_MEMORY[index] == pointer_saved_drawn) {
        VGA_MEMORY[index] = pointer_saved_original;
    }

    pointer_drawn = 0;
}

static void pointer_draw(void) {
    uint16_t index = (uint16_t)(pointer_y * VGA_WIDTH + pointer_x);
    pointer_saved_original = VGA_MEMORY[index];
    pointer_saved_drawn = invert_cell(pointer_saved_original);
    VGA_MEMORY[index] = pointer_saved_drawn;

    pointer_prev_x = pointer_x;
    pointer_prev_y = pointer_y;
    pointer_drawn = 1;
}

static void pointer_refresh(void) {
    pointer_erase_if_needed();
    pointer_draw();
}

void mouse_init(void) {
    mouse_enabled = 0;
    packet_index = 0;
    pointer_drawn = 0;
    pointer_x = VGA_WIDTH / 2;
    pointer_y = VGA_HEIGHT / 2;
    pointer_prev_x = pointer_x;
    pointer_prev_y = pointer_y;

    if (!wait_input_clear()) {
        return;
    }
    outb(0x64, 0xA8);

    if (!wait_input_clear()) {
        return;
    }
    outb(0x64, 0x20);
    if (!wait_output_full()) {
        return;
    }
    uint8_t status = inb(0x60);
    status |= 0x02;
    status &= (uint8_t)~0x20;

    if (!wait_input_clear()) {
        return;
    }
    outb(0x64, 0x60);
    if (!wait_input_clear()) {
        return;
    }
    outb(0x60, status);

    if (!mouse_write(0xF6)) {
        return;
    }
    if (!wait_output_full()) {
        return;
    }
    (void)inb(0x60);

    if (!mouse_write(0xF4)) {
        return;
    }
    if (!wait_output_full()) {
        return;
    }
    (void)inb(0x60);

    mouse_enabled = 1;
    pointer_draw();
}

void mouse_handle_byte(uint8_t data) {
    if (!mouse_enabled) {
        return;
    }

    if (packet_index == 0 && (data & 0x08) == 0) {
        return;
    }

    packet[packet_index++] = data;
    if (packet_index < 3) {
        return;
    }

    packet_index = 0;

    if (packet[0] & 0xC0) {
        return;
    }

    int dx = (int)((int8_t)packet[1]);
    int dy = (int)((int8_t)packet[2]);

    pointer_x += dx;
    pointer_y -= dy;

    if (pointer_x < 0) {
        pointer_x = 0;
    }
    if (pointer_x >= VGA_WIDTH) {
        pointer_x = VGA_WIDTH - 1;
    }
    if (pointer_y < 0) {
        pointer_y = 0;
    }
    if (pointer_y >= VGA_HEIGHT) {
        pointer_y = VGA_HEIGHT - 1;
    }

    pointer_refresh();
}
