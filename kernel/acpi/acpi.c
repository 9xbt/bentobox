#include <stdbool.h>
#include <stdint.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/mmu.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0  
};

bool acpi_use_xsdt = false;
void *acpi_rsdt;

void *acpi_find_table(const char *signature) {
    if (!memcmp(signature, "DSDT", 4))
        return fadt_dsdt;

    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    if (!acpi_use_xsdt) {
        struct acpi_rsdt *rsdt = (struct acpi_rsdt *)acpi_rsdt;
        uint32_t entries = (rsdt->sdt.length - sizeof(rsdt->sdt)) / 4;

        for (uint32_t i = 0; i < entries; i++) {
            struct acpi_sdt *sdt = VIRTUAL_HHDM(*((uint32_t *)rsdt->table + i));
            mmu_map(kernel_pd, sdt, PHYSICAL_HHDM(sdt), flags);
            if (!memcmp(sdt->signature, signature, 4)) {
                dprintf(LOG_DEBUG, "\033[93macpi:\033[0m found table '%s' at 0x%p\n", signature, sdt);
                return sdt;
            }
        }

        dprintf(LOG_DEBUG, "\033[93macpi:\033[0m couldn't find table '%s'\n", signature);
        return NULL;
    }
    
    struct acpi_xsdt *rsdt = (struct acpi_xsdt *)acpi_rsdt;
    uint32_t entries = (rsdt->sdt.length - sizeof(rsdt->sdt)) / 8;
        
    for (uint32_t i = 0; i < entries; i++) {
        struct acpi_sdt *sdt = VIRTUAL_HHDM(*((uint64_t *)rsdt->table + i));
        mmu_map(kernel_pd, sdt, PHYSICAL_HHDM(sdt), flags);
        if (!memcmp(sdt->signature, signature, 4)) {
            dprintf(LOG_DEBUG, "\033[93macpi:\033[0m found table '%s' at 0x%p\n", signature, sdt);
            return sdt;
        }
    }

    dprintf(LOG_DEBUG, "\033[93macpi:\033[0m couldn't find table '%s'\n", signature);
    return NULL;
}

void acpi_install(void) {
    struct acpi_rsdp *rsdp = VIRTUAL_HHDM(rsdp_request.response->address);

    if (!rsdp)
        panic("couldn't find ACPI");

    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF;
    #endif
    mmu_map(kernel_pd, rsdp, PHYSICAL_HHDM(rsdp), flags);
    if (rsdp->revision != 0) {
        acpi_use_xsdt = true;
        acpi_rsdt = VIRTUAL_HHDM(((struct acpi_xsdp *)rsdp)->xsdt_addr);
    } else {
        acpi_rsdt = VIRTUAL_HHDM(rsdp->rsdt_addr);
    }
    mmu_map(kernel_pd, acpi_rsdt, PHYSICAL_HHDM(acpi_rsdt), flags);
    
    dprintf(LOG_INFO, "\033[93macpi:\033[0m using %s\n", acpi_use_xsdt ? "XSDT (version 2.0)" : "RSDT (version 1.0)");

    madt_init();
    fadt_init();
    lai_init();
}