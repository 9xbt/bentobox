#include <stddef.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/mmu.h>
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
    return *((volatile uint64_t*)(hpet_address + reg));
}

__attribute__((no_sanitize("undefined")))
void hpet_write(uint32_t reg, uint64_t value) {
    *((volatile uint64_t*)(hpet_address + reg)) = value;
}

inline size_t hpet_get_ticks(void) {
    return hpet_read(HPET_REG_MAIN_COUNTER);
}

void hpet_sleep(size_t ns) {
    size_t end_ticks = hpet_get_ticks() + ns * 1000000 / hpet_period;

    while (hpet_read(HPET_REG_MAIN_COUNTER) < end_ticks) {
        asm ("pause" ::: "memory");
    }
}

void hpet_read_time(size_t *sec, size_t *nsec) {
    size_t counter = hpet_get_ticks();
    uint64_t total_nsec = counter * (hpet_period / 1000000ULL) + (counter * (hpet_period % 1000000ULL)) / 1000000ULL;

    if (sec) *sec = total_nsec / 1000000000ULL;
    if (nsec) *nsec = total_nsec % 1000000000ULL;
}

void hpet_install(void) {
    if (!(hpet = acpi_find_table("HPET")))
        return;

    hpet_address = (uint64_t)VIRTUAL_HHDM(hpet->address.address);
    mmu_map(kernel_pd, (void *)hpet_address, (void *)hpet->address.address, PTE_PRESENT | PTE_WRITABLE);
    hpet_period = hpet_read(HPET_REG_CAP) >> 32;

    hpet_write(HPET_REG_CONFIG, 0x0);
    hpet_write(HPET_REG_MAIN_COUNTER, 0);
    hpet_write(HPET_REG_CONFIG, 0x1);

    dprintf(LOG_INFO, "\033[93mhpet:\033[0m enabled HPET with %luns period\n", hpet_period / 1000000);
}