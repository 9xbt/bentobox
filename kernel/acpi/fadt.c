#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/mmu.h>

struct acpi_fadt *fadt = NULL;
struct acpi_sdt *fadt_dsdt = NULL;

void fadt_init(void) {
    if (!(fadt = (struct acpi_fadt *)acpi_find_table("FACP")))
        panic("couldn't find FADT");

    fadt_dsdt = VIRTUAL_HHDM(acpi_use_xsdt ? fadt->x_dsdt : fadt->dsdt);

    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    mmu_map(kernel_pd, fadt_dsdt, PHYSICAL_HHDM(fadt_dsdt), flags);
    for (uint32_t i = PAGE_SIZE; i < ALIGN_UP(fadt_dsdt->length, PAGE_SIZE); i += PAGE_SIZE) {
        mmu_map(kernel_pd, (void *)((uintptr_t)fadt_dsdt + i), (void *)(PHYSICAL_HHDM((uintptr_t)fadt_dsdt) + i), flags);
    }
}