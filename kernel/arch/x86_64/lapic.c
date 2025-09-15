#include <kernel/arch/x86_64/lapic.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/time.h>
#include <kernel/mmu.h>

static uint32_t lapic_ticks = 0;

static inline bool check_apic(void) {
    unsigned int eax, ebx, ecx, edx;
    asm volatile(
        "movl $1, %%eax; cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );
    return (edx & CPUID_FEAT_EDX_APIC) != 0;
}

uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t*)(VIRTUAL_HHDM(LAPIC_REGS) + reg));
}

void lapic_write(uint32_t reg, uint32_t value) {
    *((volatile uint32_t*)(VIRTUAL_HHDM(LAPIC_REGS) + reg)) = value;
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
        asm volatile ("pause" ::: "memory");
    } while (lapic_read(0x300) & (1 << 12));
}

void lapic_calibrate_timer(void) {
    asm volatile ("cli" ::: "memory");
    lapic_stop_timer();

    lapic_write(LAPIC_TIMER_DIV, 0);
    lapic_write(LAPIC_TIMER_LVT, (1 << 16) | 0xff);
    lapic_write(LAPIC_TIMER_INITCNT, 0xFFFFFFFF);

    arch_sleep(1000000);

    lapic_write(LAPIC_TIMER_LVT, LAPIC_TIMER_DISABLE);

    uint32_t ticks = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURCNT);
    lapic_ticks = ticks;
    assert(lapic_ticks != 0);

    lapic_stop_timer();
    asm volatile ("sti" ::: "memory");
}

void lapic_reinstall(void) {
    lapic_write(LAPIC_SIV, lapic_read(LAPIC_SIV) | 0x1ff);
    lapic_calibrate_timer();
}

void lapic_install(void) {
    if (!check_apic())
        panic("APIC not supported");

    mmu_map(kernel_pd, VIRTUAL_HHDM(LAPIC_REGS), (void *)LAPIC_REGS, PTE_PRESENT | PTE_WRITABLE);
    lapic_write(LAPIC_SIV, lapic_read(LAPIC_SIV) | 0x1ff);
    lapic_calibrate_timer();

    dprintf(LOG_INFO, "\033[93mapic:\033[0m enabled local APIC\n");
}