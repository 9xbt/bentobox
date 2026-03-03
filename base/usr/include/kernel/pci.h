#pragma once
#include <kernel/list.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define PCI_CAP_ID_MSI      0x05
#define PCI_IO_SPACE        (1 << 1)
#define PCI_BUS_MASTER      (1 << 2)
#define PCI_SERR_ENABLE     (1 << 8)
#define PCI_IRQ_DISABLE     (1 << 10)

#ifdef __aarch64__
#define PCIE_BASE   0x4010000000
#endif

typedef struct pci_address {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
} pci_address;

typedef struct pci_device {
    pci_address address;
    uint8_t     class;
    uint8_t     subclass;
    uint16_t    vendor_id;
    uint16_t    device_id;
} pci_device;

typedef struct pci_capability {
    uint8_t cap_id;
    uint8_t offset;
} pci_capability;


static inline uint32_t pci_config_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)((bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
}

#ifdef __aarch64__
static inline uintptr_t pcie_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return PCIE_BASE + (((uintptr_t)bus << 20) | ((uintptr_t)device << 15) | ((uintptr_t)function << 12) | offset);
}
#endif

uint32_t pci_read(pci_address addr, uint8_t offset);
void     pci_write(pci_address addr, uint8_t offset, uint32_t value);
uint16_t pci_config_read_word(pci_address addr, uint8_t offset);
void     pci_config_write_word(pci_address addr, uint8_t offset, uint16_t value);
void     pci_config_set_bits(pci_address addr, uint32_t mask);
void     pci_config_clear_bits(pci_address addr, uint32_t mask);
uint8_t  pci_find_cap(pci_device *dev, uint8_t cap_id);
list_t  *pci_find_all_caps(pci_device *dev, uint8_t cap_id);
void    *pci_map_bar(pci_device *dev, uint8_t bar, size_t pages);
pci_device *pci_get_device(uint8_t class, uint8_t subclass);
list_t     *pci_get_devices_from_vendor(uint16_t vendor);
void pci_scan(void);