#include <stdint.h>
#include <stdbool.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/mmu.h>
#include <kernel/acpi.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/assert.h>
#include <kernel/multiboot.h>

bool acpi_use_xsdt = false;
void *acpi_root_sdt;

__attribute__((no_sanitize("alignment")))
void *acpi_find_table(const char *signature) {
    if (!acpi_use_xsdt) {
        struct acpi_rsdt *rsdt = (struct acpi_rsdt*)acpi_root_sdt;
        uint32_t entries = (rsdt->sdt.length - sizeof(rsdt->sdt)) / 4;
        mmu_map(rsdt->table, rsdt->table, PTE_PRESENT);

        for (uint32_t i = 0; i < entries; i++) {
            struct acpi_sdt *sdt = (struct acpi_sdt*)(uintptr_t)(*((uint32_t*)rsdt->table + i));
            mmu_map(VIRTUAL(ALIGN_DOWN((uintptr_t)sdt, PAGE_SIZE)), (void *)ALIGN_DOWN((uintptr_t)sdt, PAGE_SIZE), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            if (!memcmp(sdt->signature, signature, 4)) {
                return VIRTUAL(sdt);
            }
        }

        dprintf("%s:%d: ACPI: couldn't find table %s\n", __FILE__, __LINE__, signature);
        return NULL;
    }
    
    /* use xsdt */
    struct acpi_xsdt *rsdt = (struct acpi_xsdt*)acpi_root_sdt;
    uint32_t entries = (rsdt->sdt.length - sizeof(rsdt->sdt)) / 8;
        
    for (uint32_t i = 0; i < entries; i++) {
        struct acpi_sdt *sdt = (struct acpi_sdt*)(uintptr_t)(*((uint32_t*)rsdt->table + i));
        mmu_map(VIRTUAL(ALIGN_DOWN((uintptr_t)sdt, PAGE_SIZE)), (void *)ALIGN_DOWN((uintptr_t)sdt, PAGE_SIZE), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        if (!memcmp(sdt->signature, signature, 4)) {
            return VIRTUAL(sdt);
        }
    }

    dprintf("%s:%d: ACPI: couldn't find table %s\n", __FILE__, __LINE__, signature);
    return NULL;
}

void *acpi_get_rsdp(void) {
#ifdef __x86_64__
    extern void *mboot;
    for (uint16_t *addr = (uint16_t*)0x000E0000; addr < (uint16_t*)0x000FFFFF; addr += 16) {
        if (!strncmp((const char*)addr, "RSD PTR ", 8)) {
            dprintf("%s:%d: found RSDP at address 0x%p\n", __FILE__, __LINE__, addr);
            return (void *)addr;
        }
    }

    void *rsdp = mboot2_find_tag(mboot, 14);
    if (rsdp != NULL) {
        dprintf("%s:%d: found RSDP at address 0x%p\n", __FILE__, __LINE__, rsdp + 8);
        return (void *)(rsdp + 8);
    }

	rsdp = mboot2_find_tag(mboot, 15);
    if (rsdp != NULL) {
        dprintf("%s:%d: found RSDP at address 0x%p\n", __FILE__, __LINE__, rsdp + 8);
        return (void *)(rsdp + 8);
    }
#else
    unimplemented;
#endif
    return NULL;
}

__attribute__((no_sanitize("undefined")))
void acpi_install() {
    struct acpi_rsdp *rsdp = acpi_get_rsdp();

    if (!rsdp)
        panic("couldn't find ACPI");

    if (rsdp->revision != 0) {
        /* use xsdt */
        acpi_use_xsdt = true;
        struct acpi_xsdp *xsdp = (struct acpi_xsdp*)rsdp;
        acpi_root_sdt = (struct acpi_xsdt*)(xsdp->xsdt_addr);
    } else {
        acpi_root_sdt = (struct acpi_xsdt*)(uintptr_t)(rsdp->rsdt_addr);
    }
    dprintf("%s:%d: ACPI version %s\n", __FILE__, __LINE__, acpi_use_xsdt ? "2.0" : "1.0");

    mmu_map(acpi_root_sdt, acpi_root_sdt, PTE_PRESENT);
    fadt_init();
    madt_init();

    printf("\033[92m * \033[97mInitialized ACPI tables\033[0m\n");
}

void acpi_reboot(void) {
#ifdef __x86_64__
    outb(fadt->reset_reg.address, fadt->reset_val);
#else
    unimplemented;
#endif
}