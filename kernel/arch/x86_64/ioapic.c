#include <stdbool.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/mmu.h>

__attribute__((no_sanitize("undefined")))
uint32_t ioapic_read(struct madt_ioapic* ioapic, uint8_t reg) {
    volatile uint32_t* ioapic_addr = (uint32_t*)(uintptr_t)ioapic->address;
    ioapic_addr[0] = reg;
    return ioapic_addr[4];
}

__attribute__((no_sanitize("undefined")))
void ioapic_write(struct madt_ioapic* ioapic, uint8_t reg, uint32_t value) {
    volatile uint32_t* ioapic_addr = (uint32_t*)(uintptr_t)ioapic->address;
    ioapic_addr[0] = reg;
    ioapic_addr[4] = value;
}

uint64_t ioapic_gsi_count(struct madt_ioapic* ioapic) {
    return (ioapic_read(ioapic, IOAPIC_VER) >> 16) & 0xff;
}

__attribute__((no_sanitize("undefined")))
struct madt_ioapic* ioapic_get_gsi(uint32_t gsi) {
    for (uint64_t i = 0; i < madt_ioapics; i++)
        if (madt_ioapic_list[i]->gsi_base <= gsi && madt_ioapic_list[i]->gsi_base + ioapic_gsi_count(madt_ioapic_list[i]) > gsi)
            return madt_ioapic_list[i];
    return NULL;
}

__attribute__((no_sanitize("undefined")))
void ioapic_redirect_gsi(uint32_t lapic_id, uint8_t vector, uint32_t gsi, uint16_t flags, bool mask) {
    struct madt_ioapic* ioapic = ioapic_get_gsi(gsi);
    
    mmu_map((void *)(uintptr_t)ioapic->address, (void *)(uintptr_t)ioapic->address, PTE_PRESENT | PTE_WRITABLE);

    uint64_t redirect = vector;

    if (flags & (1 << 1)) redirect |= (1 << 13); /* delivery mode */
    if (flags & (1 << 3)) redirect |= (1 << 15); /* destination mode */

    /* interrupt mask */
    if (mask) redirect |= (1 << 16);
    else redirect &= ~(1 << 16);

    redirect |= (uint64_t)lapic_id << 56; /* set destination field */

    uint32_t redirect_table = (gsi - ioapic->gsi_base) * 2 + 16; /* calculate the offset */
    ioapic_write(ioapic, redirect_table, (uint32_t)redirect); /* low 32 bits */
    ioapic_write(ioapic, redirect_table + 1, (uint32_t)(redirect >> 32)); /* high 32 bits */
}

__attribute__((no_sanitize("undefined")))
void ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool mask) {
    uint8_t index = 0;
    while (index < madt_isos) {
        if (madt_iso_list[index]->irq_source == irq) {
            ioapic_redirect_gsi(lapic_id, vector, madt_iso_list[index]->gsi, madt_iso_list[index]->flags, mask);
            return;
        }
        index++;
    }

    ioapic_redirect_gsi(lapic_id, vector, irq, 0, mask);
}

void ioapic_install(void) {
    struct madt_ioapic* ioapic = madt_ioapic_list[0];

    mmu_map((void *)(uintptr_t)ioapic->address, (void *)(uintptr_t)ioapic->address, PTE_PRESENT | PTE_WRITABLE);

    uint32_t id = ioapic_read(ioapic, IOAPIC_ID) >> 24;
    uint32_t count = ioapic_gsi_count(ioapic);

    if (id != ioapic->id)
        panic("APIC ID mismatch");

    for (uint32_t i = 0; i <= count; i++) {
        ioapic_write(ioapic, IOAPIC_REDTBL + (2 * i), 0x10000 | (i + 32)); /* mask interrupt */
        ioapic_write(ioapic, IOAPIC_REDTBL + (2 * i) + 1, 0); /* redirect to cpu 0 */
    }

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    asm volatile ("sti");

    dprintf("%s:%d: initialized I/O APIC with %d interrupts\n", __FILE__, __LINE__, count + 1);
    //printf("\033[92m * \033[97mInitialized I/O APIC\033[0m\n");
}