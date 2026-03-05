#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/time.h>
#include <kernel/mmu.h>

static uint32_t lapic_ticks = 0;
static bool x2apic = false;

static inline bool __check_apic(void) {
    unsigned int eax, ebx, ecx, edx;
    asm volatile(
        "movl $1, %%eax; cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );
    return (edx & CPUID_FEAT_EDX_APIC) != 0;
}

static inline bool __check_x2apic(void) {
    unsigned int eax, ebx, ecx, edx;
    asm volatile(
        "movl $1, %%eax; cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );
    return (ecx & CPUID_FEAT_ECX_X2APIC) != 0;
}

uint32_t lapic_read(uint32_t reg) {
    if (x2apic)
        return (uint32_t)rdmsr(0x800 + (reg >> 4));
    return *((volatile uint32_t*)(VIRTUAL_HHDM(LAPIC_REGS) + reg));
}

void lapic_write(uint32_t reg, uint32_t value) {
    if (x2apic)
        return wrmsr(0x800 + (reg >> 4), value);
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
    lapic_write(LAPIC_EOI, 0);
}

void lapic_ipi(uint32_t id, uint32_t irq) {
    if (x2apic) {
        wrmsr(LAPIC_ICR, ((uint64_t)id << 32) | irq);
    } else {
        lapic_write(LAPIC_ICRHI, id << LAPIC_ICDESTSHIFT);
        lapic_write(LAPIC_ICRLO, irq);
        do {
            asm volatile ("pause" ::: "memory");
        } while (lapic_read(0x300) & (1 << 12));
    }
}

void lapic_calibrate_timer(void) {
    cli();
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
    sti();
    dprintf(LOG_INFO, "\033[93mapic:\033[0m enabled local APIC timer\n");
}

void lapic_reinstall(void) {
    if (x2apic)
        wrmsr(IA32_APIC_BASE, rdmsr(IA32_APIC_BASE) | (1 << 10) | (1 << 11));
    lapic_write(LAPIC_SIV, lapic_read(LAPIC_SIV) | 0x1ff);
    sti();
}

void lapic_install(void) {
    if (!__check_apic())
        panic("APIC not supported");

    if (__check_x2apic()) {
        wrmsr(IA32_APIC_BASE, rdmsr(IA32_APIC_BASE) | (1 << 10) | (1 << 11));
        x2apic = true;
        dprintf(LOG_INFO, "\033[93mapic:\033[0m enabled x2APIC mode\n");
    } else {
        mmu_map(kernel_pd, VIRTUAL_HHDM(LAPIC_REGS), (void *)LAPIC_REGS, PTE_PRESENT | PTE_WRITABLE);
    }

    lapic_write(LAPIC_SIV, lapic_read(LAPIC_SIV) | 0x1ff);
    lapic_calibrate_timer();
    sti();
}