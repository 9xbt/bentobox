#ifdef __x86_64__
#include <kernel/arch/x86_64/io.h>
#endif
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/assert.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/time.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <lai/helpers/pm.h>
#include <lai/error.h>
#include <lai/core.h>

extern void arch_fatal_prepare(void);
extern void arch_fatal(void);

void acpi_reboot(void) {
    arch_fatal_prepare();

    lai_api_error_t err = lai_acpi_reset();
    if (err)
        dprintf(LOG_CRIT, "\033[93mlai:\033[0m %s\n", lai_api_error_to_string(err));

    arch_fatal();
    __builtin_unreachable();
}

void acpi_shutdown(void) {
    arch_fatal_prepare();

    lai_api_error_t err = lai_enter_sleep(5);
    if (err)
        dprintf(LOG_CRIT, "\033[93mlai:\033[0m %s\n", lai_api_error_to_string(err));

    arch_fatal();
    __builtin_unreachable();
}

void lai_init(void) {
    lai_set_acpi_revision(6);
    lai_create_namespace();
    lai_enable_tracing(0);
}

void *laihost_malloc(size_t n) {
    return kmalloc(n);
}

void laihost_free(void *ptr, size_t size) {
    (void)size;
    kfree(ptr);
}

void *laihost_realloc(void *ptr, size_t newsize, size_t oldsize) {
    (void)oldsize;
    return krealloc(ptr, newsize);
}

void laihost_log(int loglevel, const char *str) {
    int level = 0;
    switch (loglevel) {
        case 0:
        case 1:
        case 4:
            level = LOG_INFO;
            break;
        case 3:
            level = LOG_DEBUG;
            break;
    }
    dprintf(level, "\033[93mlai:\033[0m %s\n", str);
}

void laihost_panic(const char *str) {
    panic("%s", str);
    __builtin_unreachable();
}

void *laihost_scan(const char *sig, size_t len) {
    (void)len;
    return acpi_find_table(sig);
}

void *laihost_map(size_t phys, size_t len) {
    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    uintptr_t virt = (uintptr_t)VIRTUAL_HHDM(phys);
    for (uint32_t i = 0; i < ALIGN_UP(len, PAGE_SIZE); i += PAGE_SIZE) {
        mmu_map(kernel_pd, (void *)(virt + i), (void *)(phys + i), flags);
    }
    return (void *)virt;
}

void laihost_unmap(void *virt, size_t len) {
    for (uint32_t i = 0; i < ALIGN_UP(len, PAGE_SIZE); i += PAGE_SIZE) {
        mmu_unmap(kernel_pd, (void *)((uintptr_t)virt + i));
    }
}

#ifdef __x86_64__
void laihost_outb(uint16_t port, uint8_t val) {
    outb(port, val);
}

void laihost_outw(uint16_t port, uint16_t val) {
    outw(port, val);
}

void laihost_outd(uint16_t port, uint32_t val) {
    outl(port, val);
}

uint8_t laihost_inb(uint16_t port) {
    return inb(port);
}

uint16_t laihost_inw(uint16_t port) {
    return inw(port);
}

uint32_t laihost_ind(uint16_t port) {
    return inl(port);
}
#endif

void laihost_pci_writeb(uint16_t seg, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint8_t value) {
    (void)seg;
    uint32_t dword = pci_read(bus, device, function, offset & ~3);
    uint8_t shift = (offset & 3) * 8;
    pci_write(bus, device, function, offset & ~3, (dword & ~(0xFF << shift)) | ((uint32_t)value << shift));
}

uint8_t laihost_pci_readb(uint16_t seg, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    (void)seg;
    return (pci_read(bus, device, function, offset & ~3) >> ((offset & 3) * 8)) & 0xFF;
}

void laihost_pci_writew(uint16_t seg, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint16_t value) {
    (void)seg;
    pci_config_write_word(bus, device, function, offset, value);
}

uint16_t laihost_pci_readw(uint16_t seg, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    (void)seg;
    return pci_config_read_word(bus, device, function, offset);
}

void laihost_pci_writed(uint16_t seg, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value) {
    (void)seg;
    pci_write(bus, device, function, offset, value);
}

uint32_t laihost_pci_readd(uint16_t seg, uint8_t bus, uint8_t device, uint8_t function, uint16_t offset) {
    (void)seg;
    return pci_read(bus, device, function, offset);
}

void laihost_sleep(uint64_t ms) {
    arch_sleep(ms * 1000000UL);
}

uint64_t laihost_timer(void) {
    size_t sec, nsec;
    uptime(&sec, &nsec);
    return sec * 1000 + nsec / 1000000;
}