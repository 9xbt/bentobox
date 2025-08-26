#include <stddef.h>
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>

struct acpi_madt   *madt = NULL;
struct madt_lapic  *madt_lapic_list[SMP_MAX_CORES];
struct madt_ioapic *madt_ioapic_list[SMP_MAX_CORES];
struct madt_iso    *madt_iso_list[SMP_MAX_CORES];
struct madt_lapic_addr *lapic_addr;
size_t madt_lapics = 0;
size_t madt_ioapics = 0;
size_t madt_isos = 0;

void madt_init(void) {
    if (!(madt = (struct acpi_madt *)acpi_find_table("APIC")))
        panic("couldn't find MADT");

    uint32_t i = 0;
    while (i < madt->length - sizeof(struct acpi_madt)) {
        struct madt_entry *entry = (struct madt_entry *)(madt->table + i);

        switch (entry->type) {
            case 0:
                if (!(((struct madt_lapic*)entry)->flags & 1)) break;
                madt_lapic_list[madt_lapics++] = (struct madt_lapic*)entry;
                break;
            case 1:
                madt_ioapic_list[madt_ioapics++] = (struct madt_ioapic*)entry;
                break;
            case 2:
                madt_iso_list[madt_isos++] = (struct madt_iso*)entry;
                break;
            case 5:
                lapic_addr = (struct madt_lapic_addr*)entry;
                break;
        }

        i += entry->length;
    }

    dprintf(LOG_INFO, "\033[93macpi:\033[0m found %d Local APIC(s) and %d I/O APIC(s)\n", madt_lapics, madt_ioapics);
}