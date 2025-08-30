#include <stddef.h>
#include <stdint.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>

struct acpi_madt   *madt = NULL;
struct madt_lapic  **madt_lapic_list = NULL;
struct madt_ioapic **madt_ioapic_list = NULL;
struct madt_iso    **madt_iso_list = NULL;
size_t madt_lapics = 0;
size_t madt_ioapics = 0;
size_t madt_isos = 0;

struct madt_gicc **madt_gicc_list = NULL;
struct madt_gicd **madt_gicd_list = NULL;
size_t madt_giccs = 0;
size_t madt_gicds = 0;

void madt_init(void) {
    if (!(madt = (struct acpi_madt *)acpi_find_table("APIC")))
        panic("couldn't find MADT");

    uint32_t i = 0;
    while (i < madt->length - sizeof(struct acpi_madt)) {
        struct madt_entry *entry = (struct madt_entry *)(madt->table + i);

        switch (entry->type) {
            case 0:
                if (!(((struct madt_lapic *)entry)->flags & 1)) break;

                madt_lapic_list = krealloc(madt_lapic_list, (madt_lapics + 1) * sizeof(*madt_lapic_list));
                madt_lapic_list[madt_lapics++] = (struct madt_lapic *)entry;
                break;
            case 1:
                madt_ioapic_list = krealloc(madt_ioapic_list, (madt_ioapics + 1) * sizeof(*madt_ioapic_list));
                madt_ioapic_list[madt_ioapics++] = (struct madt_ioapic *)entry;
                break;
            case 2:
                madt_iso_list = krealloc(madt_iso_list, (madt_isos + 1) * sizeof(*madt_iso_list));
                madt_iso_list[madt_isos++] = (struct madt_iso *)entry;
                break;
            case 11:
                madt_gicc_list = krealloc(madt_gicc_list, (madt_giccs + 1) * sizeof(*madt_gicc_list));
                madt_gicc_list[madt_giccs++] = (struct madt_gicc *)entry;
                break;
            case 12:
                madt_gicd_list = krealloc(madt_gicd_list, (madt_gicds + 1) * sizeof(*madt_gicd_list));
                madt_gicd_list[madt_gicds++] = (struct madt_gicd *)entry;
                break;
        }

        i += entry->length;
    }

    #ifdef __x86_64__
    dprintf(LOG_INFO, "\033[93macpi:\033[0m found %d Local APIC(s) and %d I/O APIC(s)\n", madt_lapics, madt_ioapics);
    #elif __aarch64__
    dprintf(LOG_INFO, "\033[93macpi:\033[0m found %d GICC(s) and %d GICD(s)\n", madt_giccs, madt_gicds);
    #endif
}