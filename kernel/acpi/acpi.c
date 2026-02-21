#include <stdbool.h>
#include <stdint.h>
#include <uacpi/sleep.h>
#ifdef __x86_64__
#include <kernel/arch/x86_64/hpet.h>
#endif
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
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
        uint32_t entries = (rsdt->hdr.length - sizeof(rsdt->hdr)) / 4;

        for (uint32_t i = 0; i < entries; i++) {
            struct acpi_sdt *sdt = VIRTUAL_HHDM(*((uint32_t *)rsdt->entries + i));
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
    uint32_t entries = (rsdt->hdr.length - sizeof(rsdt->hdr)) / 8;
        
    for (uint32_t i = 0; i < entries; i++) {
        struct acpi_sdt *sdt = VIRTUAL_HHDM(*((uint64_t *)rsdt->entries + i));
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
    #ifdef __x86_64__
    hpet_install();
    #endif
    if (!args_contains("noacpi"))
        uacpi_init();
}

uint64_t acpi_get_rsdp(void) {
    return rsdp_request.response->address;
}

void acpi_reboot(void) {
    cli();

    uacpi_status ret = uacpi_reboot();
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m failed to reboot: %s\n", uacpi_status_to_string(ret));
        assert(0);
    }

    wfi();
    __builtin_unreachable();
}

void acpi_shutdown(void) {
    uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m failed to prepare for sleep: %s\n", uacpi_status_to_string(ret));
        return;
    }

    cli();

    ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(ret)) {
        dprintf(LOG_ERR, "\033[93macpi:\033[0m failed to enter sleep: %s\n", uacpi_status_to_string(ret));
        assert(0);
    }

    wfi();
    __builtin_unreachable();
}