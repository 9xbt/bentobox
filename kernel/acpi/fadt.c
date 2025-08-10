#include <stddef.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>

struct acpi_fadt *fadt = NULL;

__attribute__((no_sanitize("alignment")))
void fadt_init(void) {
#ifdef __x86_64__
    fadt = (struct acpi_fadt*)acpi_find_table("FACP");

    if (!fadt) panic("couldn't find FADT");

#if 0
    if (fadt->smi_cmd != 0 || fadt->acpi_enable != 0 || fadt->acpi_disable != 0 || (fadt->pm1a_cnt_blk & 1) == 0) {
        outb(fadt->smi_cmd, fadt->acpi_enable);
        while (!(inw(fadt->pm1a_cnt_blk) & 1));

        dprintf(6, "%s:%d: enabled ACPI mode\n", __FILE__, __LINE__);
        return;
    }
#endif
    
    dprintf(6, "%s:%d: ACPI mode is already enabled\n", __FILE__, __LINE__);
#else
    unimplemented;
#endif
}