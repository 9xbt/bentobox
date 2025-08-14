#include <stddef.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/tsc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/mmu.h>

uint64_t hpet_address = 0;
uint32_t hpet_period = 0;
struct acpi_hpet *hpet = NULL;

__attribute__((no_sanitize("undefined")))
uint64_t hpet_read(uint32_t reg) {
    return *((uint64_t*)(hpet_address + reg));
}

__attribute__((no_sanitize("undefined")))
void hpet_write(uint32_t reg, uint64_t value) {
    *((uint64_t*)(hpet_address + reg)) = value;
}

size_t hpet_get_ticks(void) {
    return hpet_read(HPET_REG_MAIN_COUNTER);
}

void hpet_sleep(size_t us) {
    if (!hpet) {
        tsc_sleep(us);
        return;
    }

    size_t end_ticks = hpet_read(HPET_REG_MAIN_COUNTER) + us * 1000000000 / hpet_period;

    while (hpet_read(HPET_REG_MAIN_COUNTER) < end_ticks) {
        asm ("pause" : : : "memory");
    }
}

void hpet_read_time(long *sec, long *nsec) {
    size_t counter = hpet_get_ticks();
    uint64_t total_nsec = (counter * (uint64_t)hpet_period) / 1000000ULL;

    if (sec) *sec = total_nsec / 1000000000ULL;
    if (nsec) *nsec = total_nsec % 1000000000ULL;
}

void hpet_install(void) {
    hpet = acpi_find_table("HPET");

    if (hpet) {
        hpet_address = (uint64_t)VIRTUAL(hpet->address);
        mmu_map((void *)hpet_address, (void *)hpet->address, PTE_PRESENT | PTE_WRITABLE);
        uint64_t cap = hpet_read(HPET_REG_CAP);
        hpet_period = (cap >> 32);
    } else return;

    dprintf(LOG_INFO, "%s:%d: 1us is %lu ticks\n", __FILE__, __LINE__, hpet_period * 1 / 1000000);

    hpet_write(HPET_REG_CONFIG, 0x0);
    hpet_write(HPET_REG_MAIN_COUNTER, 0);
    hpet_write(HPET_REG_CONFIG, 0x1);

    dprintf(LOG_INFO, "%s:%d: enabled HPET\n", __FILE__, __LINE__);
}