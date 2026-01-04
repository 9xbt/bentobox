#include <stddef.h>
#include <stdint.h>
#ifdef __x86_64__
#include <kernel/arch/x86_64/io.h>
#endif
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/list.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>

list_t *pci_devices = NULL;

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
#ifdef __x86_64__
    outl(PCI_CONFIG_ADDRESS, pci_config_addr(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
#elif defined (__aarch64__)
    return *(volatile uint32_t *)VIRTUAL_HHDM(pcie_addr(bus, device, function, offset & ~3));
#endif
}

void pci_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
#ifdef __x86_64__
    outl(PCI_CONFIG_ADDRESS, pci_config_addr(bus, device, function, offset));
    outl(PCI_CONFIG_DATA, value);
#elif defined (__aarch64__)
    *(volatile uint32_t *)VIRTUAL_HHDM(pcie_addr(bus, device, function, offset & ~3)) = value;
#endif
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return pci_read(bus, device, function, offset) >> ((offset & 2) * 8);
}

void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    pci_write(bus, device, function, offset & ~3, (pci_read(bus, device, function, offset & ~3) & (offset & 2 ? 0x0000FFFF : 0xFFFF0000)) | (offset & 2 ? (uint32_t)value << 16 : value));
}

uint8_t pci_find_cap(pci_device_t *dev, uint8_t cap_id) {
    uint8_t status = pci_config_read_word(dev->bus, dev->device, dev->function, 0x06);
    if (!(status & (1 << 4))) return 0;

    uint8_t offset = pci_read(dev->bus, dev->device, dev->function, 0x34) & 0xFF;
    while (offset) {
        uint32_t cap = pci_read(dev->bus, dev->device, dev->function, offset);
        if ((cap & 0xFF) == cap_id) return offset;
        offset = (cap >> 8) & 0xFF;
    }
    return 0;
}

void pci_check_function(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_config_read_word(bus, device, function, 0x00);
    if (vendor_id == 0xFFFF) return;
    
    pci_device_t *dev = kmalloc(sizeof(pci_device_t));
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->class = (uint8_t)(pci_config_read_word(bus, device, function, 0x0A) >> 8);
    dev->subclass = (uint8_t)(pci_config_read_word(bus, device, function, 0x0A));
    dev->vendor_id = vendor_id;
    dev->device_id = pci_config_read_word(bus, device, function, 0x02);
    list_insert(pci_devices, dev);

    dprintf(LOG_DEBUG, "\033[93mpci:\033[0m %02x:%02x.%u: %04x:%04x [%02x:%02x]\n", bus, device, function, vendor_id, dev->device_id, dev->class, dev->subclass);
}

void pci_check_device(uint8_t bus, uint8_t device) {
    uint16_t vendor_id = pci_config_read_word(bus, device, 0, 0x00);
    if (vendor_id == 0xFFFF) return;
    
    pci_check_function(bus, device, 0);
    
    uint8_t header_type = pci_config_read_word(bus, device, 0, 0x0E) & 0xFF;
    if (header_type & 0x80) {
        for (uint8_t function = 1; function < 8; function++) {
            pci_check_function(bus, device, function);
        }
    }
}

void pci_check_bus(uint8_t bus) {
    for (uint8_t device = 0; device < 32; device++) {
        pci_check_device(bus, device);
    }
}

pci_device_t *pci_get_device(uint8_t class, uint8_t subclass) {
    foreach(i, pci_devices) {
        pci_device_t *dev = i->value;
        if (dev->class == class && dev->subclass == subclass) {
            return dev;
        }
    }
    return NULL;
}

pci_device_t *pci_get_device_by_vendor(uint16_t vendor, uint16_t device) {
    foreach(i, pci_devices) {
        pci_device_t *dev = i->value;
        if (dev->vendor_id == vendor && dev->device_id == device) {
            return dev;
        }
    }
    return NULL;
}

void pci_scan(void) {
    pci_devices = list_create();

    #ifdef __aarch64__
    for (uintptr_t i = 0; i < 0x10000000; i += PAGE_SIZE) {
        mmu_map(kernel_pd, VIRTUAL_HHDM(PCIE_BASE + i), (void *)(PCIE_BASE + i), PTE_VALID | PTE_AF | PTE_RW | PTE_UXN | PTE_PXN);
    }
    #endif

    dprintf(LOG_INFO, "\033[93mpci:\033[0m finding PCI devices\n");

    uint8_t function, bus;
    uint16_t header_type = (uint8_t)(pci_config_read_word(0, 0, 0, 0x0E));
    if ((header_type & 0x80) == 0) {
        // single PCI host controller
        pci_check_bus(0);
    } else {
        // multiple PCI host controllers
        for (function = 0; function < 8; function++) {
            if (pci_config_read_word(0, 0, function, 0x02) != 0xFFFF) break;
            bus = function;
            pci_check_bus(bus);
        }
    }
}