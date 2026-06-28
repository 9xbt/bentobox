#include <stdbool.h>
#include <stddef.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/irq.h>
#include <kernel/mmu.h>

uint32_t ioapic_read(struct madt_ioapic* ioapic, uint8_t reg) {
    volatile uint32_t* ioapic_addr = (volatile uint32_t*)VIRTUAL_HHDM(ioapic->address);
    ioapic_addr[0] = reg;
    return ioapic_addr[4];
}

void ioapic_write(struct madt_ioapic* ioapic, uint8_t reg, uint32_t value) {
    volatile uint32_t* ioapic_addr = (volatile uint32_t*)VIRTUAL_HHDM(ioapic->address);
    ioapic_addr[0] = reg;
    ioapic_addr[4] = value;
}

uint64_t ioapic_gsi_count(struct madt_ioapic* ioapic) {
    return (ioapic_read(ioapic, IOAPIC_VER) >> 16) & 0xff;
}

struct madt_ioapic* ioapic_get_gsi(uint32_t gsi) {
    for (uint64_t i = 0; i < madt_ioapics; i++)
        if (madt_ioapic_list[i]->gsi_base <= gsi &&
            gsi <= madt_ioapic_list[i]->gsi_base + ioapic_gsi_count(madt_ioapic_list[i]))
            return madt_ioapic_list[i];
    return NULL;
}

void ioapic_redirect_gsi(uint32_t lapic_id, uint8_t vector, uint32_t gsi, uint16_t flags, bool mask) {
    struct madt_ioapic *ioapic = ioapic_get_gsi(gsi);
    if (!ioapic) {
        dprintf(LOG_ERR, "\033[93mapic:\033[0m failed to redirect GSI %u\n", gsi);
        return;
    }

    uint64_t redirect = vector;

    if (flags & (1 << 1))
        redirect |= IOAPIC_DELIVERY_MODE_INIT;
    if (flags & (1 << 3))
        redirect |= IOAPIC_DEST_MODE_LOGICAL;

    if (mask)
        redirect |= IOAPIC_INT_MASK;

    redirect |= (uint64_t)lapic_id << IOAPIC_DEST_FIELD_SHIFT;

    uint32_t redirect_table = (gsi - ioapic->gsi_base) * 2 + 16;
    ioapic_write(ioapic, redirect_table, (uint32_t)redirect);
    ioapic_write(ioapic, redirect_table + 1, (uint32_t)(redirect >> 32));
}

void ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool mask) {
    for (uint8_t index = 0; index < madt_isos; index++) {
        if (madt_iso_list[index]->irq_source == irq) {
            ioapic_redirect_gsi(lapic_id, vector, madt_iso_list[index]->gsi, madt_iso_list[index]->flags, mask);
            return;
        }
    }
    ioapic_redirect_gsi(lapic_id, vector, irq, 0, mask);
}

void ioapic_domain_alloc(struct irq_domain *domain, int virq, int hwirq) {
    ioapic_redirect_irq(0, domain->base + virq, hwirq, false);
}

void ioapic_domain_free(struct irq_domain *domain, int virq, int hwirq) {
    ioapic_redirect_irq(0, domain->base + virq, hwirq, true);
}

irq_domain_t *ioapic_domain = NULL;

void ioapic_install(void) {
    struct madt_ioapic *ioapic = madt_ioapic_list[0];

    mmu_map(kernel_pd, VIRTUAL_HHDM((uintptr_t)ioapic->address), (void *)(uintptr_t)ioapic->address, PTE_PRESENT | PTE_WRITABLE);

    uint32_t id = ioapic_read(ioapic, IOAPIC_ID) >> 24;
    uint32_t count = ioapic_gsi_count(ioapic);

    if (id != ioapic->id)
        dprintf(LOG_WARNING, "\033[93mapic:\033[0m APIC ID mismatch, expected %u but got %u\n", ioapic->id, id);

    for (uint32_t i = 0; i <= count; i++)
        ioapic_redirect_irq(0, i + 32, i, true);

    dprintf(LOG_INFO, "\033[93mapic:\033[0m I/O APIC #%u handling GSI %u-%u\n", 0, ioapic->gsi_base, ioapic->gsi_base + count);

    ioapic_domain = irq_create_domain(lapic_domain->chip, lapic_domain, ioapic->gsi_base, count, ioapic_domain_alloc, ioapic_domain_free);
}