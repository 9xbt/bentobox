#include <stddef.h>
#include <stdint.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/pci.h>

struct pci_device primary_bus[32];

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
#ifdef __x86_64__
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) |
        (function << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
#else
    unimplemented;
    return 0;
#endif
}

void pci_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
#ifdef __x86_64__
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) |
        (function << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
#else
    unimplemented;
#endif
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return pci_read(bus, device, function, offset) >> ((offset & 2) * 8);
}

uint8_t pci_get_secondary_bus(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t header_type = (uint8_t)(pci_config_read_word(bus, device, function, 0x0E));

    if (header_type != 0x1) {
        return 0xFF;
    }

    return (uint8_t)(pci_config_read_word(bus, device, function, 0x18) >> 8);
}

int pci_find_capability(struct pci_device *dev, uint8_t cap_id) {
    uint8_t status = pci_read(dev->bus, dev->device, 0x00, 0x06) >> 16;
    if (!(status & (1 << 4))) return 0;

    uint8_t offset = pci_read(dev->bus, dev->device, 0x00, 0x34) & 0xFF;
    while (offset) {
        uint32_t cap = pci_read(dev->bus, dev->device, 0x00, offset);
        if ((cap & 0xFF) == cap_id) return offset;
        offset = (cap >> 8) & 0xFF;
    }
    return 0;
}

void pci_check_device(uint8_t bus, uint8_t device) {
    uint8_t function = 0;
    uint16_t vendor_id = pci_config_read_word(bus, device, 0, 0x02);
    uint16_t device_id = pci_config_read_word(bus, device, 0, 0x00);
    uint8_t class = (uint8_t)(pci_config_read_word(bus, device, function, 0x0A) >> 8);
    uint8_t subclass = (uint8_t)(pci_config_read_word(bus, device, function, 0x0A));

    if (vendor_id == 0xFFFF) return;

    pci_check_function(bus, device, function);
    uint16_t header_type = (uint8_t)(pci_config_read_word(bus, device, function, 0x0E));
    
    if ((header_type & 0x80) != 0) {
        for (function = 1; function < 8; function++) {
            if (pci_config_read_word(bus, device, function, 0x02) != 0xFFFF) {
                pci_check_function(bus, device, function);
            }
        }
    }

    if (bus == 0) {
        primary_bus[device].bus = 0;
        primary_bus[device].device = device;
        primary_bus[device].class = class;
        primary_bus[device].subclass = subclass;
        primary_bus[device].vendor_id = vendor_id;
        primary_bus[device].device_id = device_id;
    }
}

void pci_check_bus(uint8_t bus) {
    uint8_t device;

    for (device = 0; device < 32; device++) {
        pci_check_device(bus, device);
    }
}

void pci_check_function(uint8_t bus, uint8_t device, uint8_t function) {
    uint8_t base_class = (uint8_t)(pci_config_read_word(bus, device, function, 0x0A) >> 8);
    uint8_t sub_class = (uint8_t)(pci_config_read_word(bus, device, function, 0x0A));
    uint8_t secondary_bus;

    if ((base_class == 0x6) && (sub_class == 0x4)) {
        secondary_bus = pci_get_secondary_bus(bus, device, function);
        pci_check_bus(secondary_bus);
    }
}

struct pci_device *pci_get_device(uint8_t class, uint8_t subclass) {
    for (int i = 0; i < 32; i++) {
        if (primary_bus[i].class == class && primary_bus[i].subclass == subclass) {
            return &primary_bus[i];
        }
    }
    return NULL;
}

struct pci_device *pci_get_device_from_vendor(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < 32; i++) {
        if (primary_bus[i].vendor_id == vendor && primary_bus[i].device_id == device) {
            return &primary_bus[i];
        }
    }
    return NULL;
}

void pci_scan(void) {
    dprintf("%s:%d: finding PCI devices\n", __FILE__, __LINE__);
    uint8_t function;
    uint8_t bus;

    uint16_t header_type = (uint8_t)(pci_config_read_word(0, 0, 0, 0x0E));
    if ((header_type & 0x80) == 0) {
        /* single PCI host controller */
        pci_check_bus(0);
    } else {
        /* multiple PCI host controllers */
        for (function = 0; function < 8; function++) {
            if (pci_config_read_word(0, 0, function, 0x02) != 0xFFFF) break;
            bus = function;
            pci_check_bus(bus);
        }
    }
    //printf("\033[92m * \033[97mInitialized PCI\033[0m\n");
}