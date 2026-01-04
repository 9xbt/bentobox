#pragma once
#include <stdint.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define PCI_CAP_ID_MSI      0x05
#define PCI_IO_SPACE        (1 << 1)
#define PCI_BUS_MASTER      (1 << 2)
#define PCI_SERR_ENABLE     (1 << 8)

#define PCIE_BASE       0x4010000000

typedef struct pci_device {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  class;
    uint8_t  subclass;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_t;

static inline uint32_t pci_config_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
}

static inline uintptr_t pcie_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return PCIE_BASE + (((uintptr_t)bus << 20) | ((uintptr_t)device << 15) | ((uintptr_t)function << 12) | offset);
}

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void     pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
uint8_t  pci_find_cap(pci_device_t *dev, uint8_t cap_id);
pci_device_t *pci_get_device(uint8_t class, uint8_t subclass);
pci_device_t *pci_get_device_by_vendor(uint16_t vendor, uint16_t device);
void pci_scan(void);