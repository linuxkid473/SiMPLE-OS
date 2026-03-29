#ifndef SIMPLE_ATA_H
#define SIMPLE_ATA_H

#include "types.h"

int ata_read_sector(uint32_t lba, uint8_t* buffer);
int ata_write_sector(uint32_t lba, const uint8_t* buffer);

#endif
