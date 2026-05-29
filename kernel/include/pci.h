#ifndef SIMPLE_PCI_H
#define SIMPLE_PCI_H

#include "types.h"

/* PCI configuration space offsets */
#define PCI_VENDOR_ID    0x00
#define PCI_DEVICE_ID    0x02
#define PCI_COMMAND      0x04
#define PCI_STATUS       0x06
#define PCI_REVISION     0x08
#define PCI_PROGIF       0x09
#define PCI_SUBCLASS     0x0A
#define PCI_CLASS        0x0B
#define PCI_HEADER_TYPE  0x0E
#define PCI_BAR0         0x10
#define PCI_BAR1         0x14
#define PCI_INT_LINE     0x3C
#define PCI_INT_PIN      0x3D

#define PCI_CMD_BUSMASTER (1 << 2)
#define PCI_CMD_MMIO      (1 << 1)

/* Read 32/16/8 bits from PCI config space (bus/dev/fn, offset). */
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
uint8_t  pci_read8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val);
void     pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint16_t val);
void     pci_write8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint8_t val);

/* Find first device matching class/subclass/progif.
 * Returns 1 and fills *bus/*dev/*fn on success, 0 if not found. */
int pci_find_device(uint8_t cls, uint8_t sub, uint8_t prog,
                    uint8_t *bus, uint8_t *dev, uint8_t *fn);

/* Find Nth matching device (n=0 is first).
 * Returns 1 on success. */
int pci_find_device_n(uint8_t cls, uint8_t sub, uint8_t prog, int n,
                      uint8_t *bus, uint8_t *dev, uint8_t *fn);

/* Enable bus mastering (required for DMA). */
void pci_enable_bus_mastering(uint8_t bus, uint8_t dev, uint8_t fn);

/* Return base address of BAR (bar=0..5), masking the type bits.
 * Only handles 32-bit memory BARs; returns 0 for I/O BARs. */
uint32_t pci_bar_base(uint8_t bus, uint8_t dev, uint8_t fn, int bar);

#endif
