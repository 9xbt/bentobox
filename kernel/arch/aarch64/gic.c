#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <stdint.h>

uint32_t gicd_read(struct madt_gicd *gicd, uint32_t offset) {
    volatile uint32_t *base = VIRTUAL_HHDM(gicd->phys_base);
    return base[offset / 4];
}

void gicd_write(struct madt_gicd *gicd, uint32_t offset, uint32_t value) {
    volatile uint32_t *base = VIRTUAL_HHDM(gicd->phys_base);
    base[offset / 4] = value;
}

uint32_t gicc_read(struct madt_gicc *gicc, uint32_t offset) {
    volatile uint32_t *base = VIRTUAL_HHDM(gicc->phys_base);
    return base[offset / 4];
}

void gicc_write(struct madt_gicc *gicc, uint32_t offset, uint32_t value) {
    volatile uint32_t *base = VIRTUAL_HHDM(gicc->phys_base);
    base[offset / 4] = value;
}

void gic_send_sgi(uint8_t sgiid, uint8_t mask) {
    gicd_write(madt_gicd_list[0], GICD_SGIR, (sgiid & 0xF) | ((mask & 0xFF) << 16));
}

void gic_install(void) {
    if (!madt_gicds || !madt_giccs)
        panic("couldn't find GIC");
    struct madt_gicd *gicd = madt_gicd_list[0];
    if (gicd->version != 2)
        panic("unsupported GIC version");

    mmu_map(kernel_pd, VIRTUAL_HHDM(gicd->phys_base), (void*)gicd->phys_base, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN);
    gicd_write(gicd, GICD_CTLR, true);
    gicd_write(gicd, GICD_ISENABLER0, 0x0000FFFF);

    dprintf(LOG_INFO, "\033[93mgic:\033[0m initialized distributor\n");

    for (size_t i = 0; i < madt_giccs; i++) {
        struct madt_gicc *gicc = madt_gicc_list[i];
        mmu_map(kernel_pd, VIRTUAL_HHDM(gicc->phys_base), (void*)gicc->phys_base, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN);
        cpu_list[i]->gicc = gicc;
    }

    gicc_write(this_cpu->gicc, GICC_PMR, 0xFF);
    gicc_write(this_cpu->gicc, GICC_CTLR, 1);

    dprintf(LOG_INFO, "\033[93mgic:\033[0m initialized CPU interfaces\n");
    
    asm volatile ("msr daifclr, #2");

    gicd_write(gicd, GICD_ISENABLER0, 1 << 29);
}