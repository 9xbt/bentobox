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

uint32_t pci_read(pci_address addr, uint8_t offset) {
#ifdef __x86_64__
    outl(PCI_CONFIG_ADDRESS, pci_config_addr(addr.bus, addr.device, addr.function, offset));
    return inl(PCI_CONFIG_DATA);
#elif defined (__aarch64__)
    return *(volatile uint32_t *)VIRTUAL_HHDM(pcie_addr(addr.bus, addr.device, addr.function, offset & ~3));
#endif
}

void pci_write(pci_address addr, uint8_t offset, uint32_t value) {
#ifdef __x86_64__
    outl(PCI_CONFIG_ADDRESS, pci_config_addr(addr.bus, addr.device, addr.function, offset));
    outl(PCI_CONFIG_DATA, value);
#elif defined (__aarch64__)
    *(volatile uint32_t *)VIRTUAL_HHDM(pcie_addr(addr.bus, addr.device, addr.function, offset & ~3)) = value;
#endif
}

uint16_t pci_config_read_word(pci_address addr, uint8_t offset) {
    return pci_read(addr, offset) >> ((offset & 2) * 8);
}

void pci_config_write_word(pci_address addr, uint8_t offset, uint16_t value) {
    pci_write(addr, offset & ~3, (pci_read(addr, offset & ~3) & (offset & 2 ? 0x0000FFFF : 0xFFFF0000)) | (offset & 2 ? (uint32_t)value << 16 : value));
}

void pci_config_set_bits(pci_address addr, uint32_t mask) {
    pci_config_write_word(addr, 0x04, pci_config_read_word(addr, 0x04) | mask);
}

void pci_config_clear_bits(pci_address addr, uint32_t mask) {
    pci_config_write_word(addr, 0x04, pci_config_read_word(addr, 0x04) & mask);
}

uint8_t pci_find_cap(pci_device *dev, uint8_t cap_id) {
    uint8_t status = pci_config_read_word(dev->address, 0x06);
    if (!(status & (1 << 4)))
        return 0;

    uint8_t offset = pci_read(dev->address, 0x34) & 0xFF;
    while (offset) {
        uint32_t cap = pci_read(dev->address, offset);
        if ((cap & 0xFF) == cap_id)
            return offset;
        offset = (cap >> 8) & 0xFF;
    }
    return 0;
}

list_t *pci_find_all_caps(pci_device *dev, uint8_t cap_id) {
    list_t *caps = list_create();
    
    uint8_t status = pci_config_read_word(dev->address, 0x06);
    if (!(status & (1 << 4)))
        return caps;

    uint8_t offset = pci_read(dev->address, 0x34) & 0xFF;
    while (offset) {
        uint32_t cap = pci_read(dev->address, offset);
        if ((cap & 0xFF) == cap_id) {
            pci_capability *found = kmalloc(sizeof(pci_capability));
            found->cap_id = cap_id;
            found->offset = offset;
            list_insert(caps, found);
        }
        offset = (cap >> 8) & 0xFF;
    }
    
    return caps;
}

void *pci_map_bar(pci_device *dev, uint8_t bar, size_t pages) {
    uint32_t bar_offset = 0x10 + (bar * 4);
    uint32_t bar_low = pci_read(dev->address, bar_offset);
    uint64_t phys_base;

    if ((bar_low & 0x6) == 0x4) {
        uint32_t bar_high = pci_read(dev->address, bar_offset + 4);
        phys_base = ((uint64_t)bar_high << 32) | (bar_low & 0xFFFFFFF0);
    } else if ((bar_low & 0x6) == 0x0) {
        phys_base = bar_low & 0xFFFFFFF0;
    } else {
        dprintf(LOG_ERR, "\033[93mpci:\033[0m unsupported BAR type\n");
        return NULL;
    }

    void *base = VIRTUAL_HHDM(phys_base);

    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif
    for (size_t i = 0; i < pages * PAGE_SIZE; i += PAGE_SIZE) {
        mmu_map(kernel_pd, (void *)(base + i), (void *)(phys_base + i), flags);
    }

    return base;
}

void pci_check_function(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor_id = pci_config_read_word((pci_address){ bus, device, function }, 0x00);
    if (vendor_id == 0xFFFF)
        return;
    
    pci_device *dev = kmalloc(sizeof(pci_device));
    dev->address   = (pci_address){ bus, device, function };
    dev->class     = (uint8_t)(pci_config_read_word(dev->address, 0x0A) >> 8);
    dev->subclass  = (uint8_t)(pci_config_read_word(dev->address, 0x0A));
    dev->vendor_id = vendor_id;
    dev->device_id = pci_config_read_word(dev->address, 0x02);
    list_insert(pci_devices, dev);

    dprintf(LOG_DEBUG, "\033[93mpci:\033[0m %02x:%02x.%u: %04x:%04x [%02x:%02x]\n", bus, device, function, vendor_id, dev->device_id, dev->class, dev->subclass);
}

void pci_check_device(uint8_t bus, uint8_t device) {
    uint16_t vendor_id = pci_config_read_word((pci_address){ bus, device, 0 }, 0x00);
    if (vendor_id == 0xFFFF)
        return;
    
    pci_check_function(bus, device, 0);
    
    uint8_t header_type = pci_config_read_word((pci_address){ bus, device, 0 }, 0x0E) & 0xFF;
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

pci_device *pci_get_device(uint8_t class, uint8_t subclass) {
    foreach_safe(i, pci_devices) {
        pci_device *dev = i->value;
        if (dev->class == class && dev->subclass == subclass)
            return dev;
    }
    return NULL;
}

list_t *pci_get_devices_from_vendor(uint16_t vendor) {
    list_t *devices = list_create();
    foreach_safe(i, pci_devices) {
        pci_device *dev = i->value;
        if (dev->vendor_id == vendor)
            list_insert(devices, dev);
    }
    return devices;
}

void pci_scan(void) {
    pci_devices = list_create();

    #ifdef __aarch64__
    for (uintptr_t i = 0; i < 0x10000000; i += PAGE_SIZE) {
        mmu_map(kernel_pd, VIRTUAL_HHDM(PCIE_BASE + i), (void *)(PCIE_BASE + i), PTE_VALID | PTE_AF | PTE_RW | PTE_UXN | PTE_PXN);
    }
    #endif

    dprintf(LOG_INFO, "\033[93mpci:\033[0m finding PCI devices\n");

    uint8_t  function, bus;
    uint16_t header_type = (uint8_t)(pci_config_read_word((pci_address){ 0, 0, 0 }, 0x0E));
    if ((header_type & 0x80) == 0) {
        pci_check_bus(0);
    } else {
        for (function = 0; function < 8; function++) {
            if (pci_config_read_word((pci_address){ 0, 0, function}, 0x02) != 0xFFFF)
                break;
            bus = function;
            pci_check_bus(bus);
        }
    }
}