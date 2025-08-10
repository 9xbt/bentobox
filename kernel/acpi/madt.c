#include <stddef.h>
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>

struct acpi_madt *madt = NULL;
struct madt_lapic *madt_lapic_list[SMP_MAX_CORES];
struct madt_ioapic *madt_ioapic_list[SMP_MAX_CORES];
struct madt_iso *madt_iso_list[SMP_MAX_CORES];
uint32_t madt_lapics = 0;
uint32_t madt_ioapics = 0;
uint32_t madt_isos = 0;

struct madt_lapic_addr *lapic_addr;

__attribute__((no_sanitize("alignment")))
void madt_init() {
    madt = (struct acpi_madt*)acpi_find_table("APIC");

    if (!madt) panic("couldn't find MADT");

    dprintf(6, "%s:%d: MADT address: 0x%p\n", __FILE__, __LINE__, (uintptr_t)madt);

    uint32_t i = 0;
    while (i < madt->length - sizeof(struct acpi_madt)) {
        struct madt_entry *entry = (struct madt_entry*)(madt->table + i);

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
            //case 9:
            //    madt_lapics++;
            //    break;
        }

        i += entry->length;
    }
    dprintf(6, "%s:%d: system has %d Local APIC%s and %d I/O APIC%s\n", __FILE__, __LINE__, madt_lapics, madt_lapics == 1 ? "" : "s", madt_ioapics, madt_ioapics == 1 ? "" : "s");
}