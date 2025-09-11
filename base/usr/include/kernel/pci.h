#pragma once
#include <stdint.h>

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define PCI_CAP_ID_MSI      0x05
#define PCI_IO_SPACE        (1 << 1)
#define PCI_BUS_MASTER      (1 << 2)
#define PCI_SERR_ENABLE     (1 << 8)

struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t class;
    uint8_t subclass;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
};

void pci_scan(void);
void pci_check_device(uint8_t bus, uint8_t device);
void pci_check_bus(uint8_t bus);
void pci_check_function(uint8_t bus, uint8_t device, uint8_t function);
uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
uint8_t pci_get_irq_line(uint8_t bus, uint8_t device, uint8_t function);
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t function);
uint16_t pci_get_device_id(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_class(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_progif(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_header_type(uint8_t bus, uint8_t device, uint8_t function);
uint8_t pci_get_secondary_bus(uint8_t bus, uint8_t device, uint8_t function);
int pci_find_capability(struct pci_device *dev, uint8_t cap_id);
struct pci_device *pci_get_device(uint8_t class, uint8_t subclass);
struct pci_device *pci_get_device_from_vendor(uint16_t vendor, uint16_t device);