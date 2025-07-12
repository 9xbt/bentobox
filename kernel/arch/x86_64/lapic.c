#include <cpuid.h>
#include <kernel/acpi.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/tsc.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/mmu.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/assert.h>

static uint32_t lapic_ticks = 0;

static bool cpu_check_apic(void) {
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return edx & CPUID_FEAT_EDX_APIC;
    return false;
}

static void pic_mask_all_irqs(void) {
    outb(0x21, 0xFF);
    outb(0x21, 0xFF);
    outb(0x20, 0x20);
    outb(0x20, 0x20);
}

__attribute__((no_sanitize("undefined")))
uint32_t lapic_read(uint32_t reg) {
    return *((uint32_t*)(VIRTUAL(LAPIC_REGS) + reg));
}

__attribute__((no_sanitize("undefined")))
void lapic_write(uint32_t reg, uint32_t value) {
    *((uint32_t*)(VIRTUAL(LAPIC_REGS) + reg)) = value;
}

void lapic_stop_timer(void) {
    lapic_write(LAPIC_TIMER_INITCNT, 0);
    lapic_write(LAPIC_TIMER_LVT, LAPIC_TIMER_DISABLE);
}

void lapic_oneshot(uint8_t vector, uint32_t ms) {
    lapic_stop_timer();

    lapic_write(LAPIC_TIMER_DIV, 0);
    lapic_write(LAPIC_TIMER_LVT, vector);
    lapic_write(LAPIC_TIMER_INITCNT, lapic_ticks * ms);
}

void lapic_eoi(void) {
    lapic_write((uint8_t)LAPIC_EOI, 0);
}

void lapic_ipi(uint32_t id, uint32_t irq) {
    lapic_write(LAPIC_ICRHI, id << LAPIC_ICDESTSHIFT);
    lapic_write(LAPIC_ICRLO, irq);
    do {
        asm volatile ("pause" : : : "memory");
    } while (lapic_read(0x300) & (1 << 12));
}

void lapic_calibrate_timer(void) {
    asm volatile ("cli" : : : "memory");
    lapic_stop_timer();

    lapic_write(LAPIC_TIMER_DIV, 0);
    lapic_write(LAPIC_TIMER_LVT, (1 << 16) | 0xff);
    lapic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);

    hpet_sleep(1000);

    lapic_write(LAPIC_TIMER_LVT, LAPIC_TIMER_DISABLE);

    uint32_t ticks = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURCNT);
    lapic_ticks = ticks;
    assert(lapic_ticks != 0);

    lapic_stop_timer();
    asm volatile ("sti" : : : "memory");
}

__attribute__((no_sanitize("undefined")))
void lapic_install(void) {
    assert(acpi_root_sdt);
    if (!cpu_check_apic())
        panic("APIC not supported");

    pic_mask_all_irqs();
    mmu_map(VIRTUAL(LAPIC_REGS), (void *)LAPIC_REGS, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    lapic_write(LAPIC_SIV, lapic_read(LAPIC_SIV) | 0x100);
}