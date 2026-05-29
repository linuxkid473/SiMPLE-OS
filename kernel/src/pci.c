#include "pci.h"
#include "io.h"
#include "serial.h"

#define PCI_ADDR_PORT  0x0CF8
#define PCI_DATA_PORT  0x0CFC

static uint32_t pci_mk_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return (1u << 31)
         | ((uint32_t)bus << 16)
         | ((uint32_t)(dev & 0x1F) << 11)
         | ((uint32_t)(fn  & 0x07) <<  8)
         | (off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(PCI_ADDR_PORT, pci_mk_addr(bus, dev, fn, off));
    return inl(PCI_DATA_PORT);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t v = pci_read32(bus, dev, fn, (uint8_t)(off & 0xFC));
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t v = pci_read32(bus, dev, fn, (uint8_t)(off & 0xFC));
    return (uint8_t)((v >> ((off & 3) * 8)) & 0xFF);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val) {
    outl(PCI_ADDR_PORT, pci_mk_addr(bus, dev, fn, off));
    outl(PCI_DATA_PORT, val);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint16_t val) {
    uint32_t cur = pci_read32(bus, dev, fn, (uint8_t)(off & 0xFC));
    uint32_t shift = (off & 2) * 8;
    cur = (cur & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, dev, fn, (uint8_t)(off & 0xFC), cur);
}

void pci_write8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint8_t val) {
    uint32_t cur = pci_read32(bus, dev, fn, (uint8_t)(off & 0xFC));
    uint32_t shift = (off & 3) * 8;
    cur = (cur & ~(0xFFu << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, dev, fn, (uint8_t)(off & 0xFC), cur);
}

int pci_find_device_n(uint8_t cls, uint8_t sub, uint8_t prog, int n,
                      uint8_t *out_bus, uint8_t *out_dev, uint8_t *out_fn)
{
    int found = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            /* Check if device exists (vendor != 0xFFFF) */
            uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF) continue;

            /* Check header type for multi-function */
            uint8_t hdr = pci_read8((uint8_t)bus, (uint8_t)dev, 0, PCI_HEADER_TYPE);
            int max_fn = (hdr & 0x80) ? 8 : 1;

            for (int fn = 0; fn < max_fn; fn++) {
                uint16_t v = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, PCI_VENDOR_ID);
                if (v == 0xFFFF) continue;

                uint8_t c = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, PCI_CLASS);
                uint8_t s = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, PCI_SUBCLASS);
                uint8_t p = pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, PCI_PROGIF);

                if (c == cls && s == sub && p == prog) {
                    if (found == n) {
                        *out_bus = (uint8_t)bus;
                        *out_dev = (uint8_t)dev;
                        *out_fn  = (uint8_t)fn;
                        return 1;
                    }
                    found++;
                }
            }
        }
    }
    return 0;
}

int pci_find_device(uint8_t cls, uint8_t sub, uint8_t prog,
                    uint8_t *bus, uint8_t *dev, uint8_t *fn) {
    return pci_find_device_n(cls, sub, prog, 0, bus, dev, fn);
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint16_t cmd = pci_read16(bus, dev, fn, PCI_COMMAND);
    cmd |= PCI_CMD_BUSMASTER | PCI_CMD_MMIO;
    pci_write16(bus, dev, fn, PCI_COMMAND, cmd);
}

uint32_t pci_bar_base(uint8_t bus, uint8_t dev, uint8_t fn, int bar) {
    uint8_t off = (uint8_t)(PCI_BAR0 + bar * 4);
    uint32_t raw = pci_read32(bus, dev, fn, off);
    if (raw & 1) return 0;            /* I/O BAR, not supported here */
    return raw & 0xFFFFFFF0u;
}
