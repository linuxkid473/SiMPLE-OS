#include "ata.h"
#include "io.h"

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT0  0x1F2
#define ATA_LBA0       0x1F3
#define ATA_LBA1       0x1F4
#define ATA_LBA2       0x1F5
#define ATA_HDDEVSEL   0x1F6
#define ATA_COMMAND    0x1F7
#define ATA_STATUS     0x1F7

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_CACHE_FLUSH 0xE7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static int ata_wait_ready(void) {
    uint8_t status;
    do {
        status = inb(ATA_STATUS);
    } while (status & ATA_SR_BSY);

    if (status & ATA_SR_ERR) {
        return -1;
    }

    while ((status & ATA_SR_DRQ) == 0) {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
    }

    return 0;
}

static void ata_select_drive(uint32_t lba) {
    outb(ATA_HDDEVSEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    io_wait();
    io_wait();
    io_wait();
    io_wait();
}

int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    ata_select_drive(lba);

    outb(ATA_SECCOUNT0, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    if (ata_wait_ready() != 0) {
        return -1;
    }

    insw(ATA_DATA, buffer, 256);
    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    ata_select_drive(lba);

    outb(ATA_SECCOUNT0, 1);
    outb(ATA_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    if (ata_wait_ready() != 0) {
        return -1;
    }

    outsw(ATA_DATA, buffer, 256);
    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);

    uint8_t status;
    do {
        status = inb(ATA_STATUS);
    } while (status & ATA_SR_BSY);

    return (status & ATA_SR_ERR) ? -1 : 0;
}
