#include <stddef.h>
#include <stdint.h>
#include <cpuid.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/pit.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#define TSC_CALIBRATION_PERIOD 100000

uint64_t tsc_period = 0;
static uint64_t delta = 0;

static inline uint64_t rdtsc(void) {
    unsigned int lo, hi;
    asm volatile (
        "lfence\n"
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

void tsc_install(void) {
    unsigned int eax, ebx, ecx, edx;
    if (!__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) ||
        eax < 0x80000007 ||
        !__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx) ||
        (edx & (1 << 8)) == 0) {
        if (hpet) {
            dprintf(6, "%s:%d: invariant TSC not supported\n", __FILE__, __LINE__);
            return;
        } else {
            panic("Invariant TSC not supported");
        }
    }

    if (hpet) {
        asm volatile ("cli");
        uint64_t start = rdtsc();
        hpet_sleep(TSC_CALIBRATION_PERIOD);
        delta = rdtsc() - start;
        asm volatile ("sti");
    } else {
        uint64_t start = rdtsc();
        pit_oneshot(TSC_CALIBRATION_PERIOD);
        delta = rdtsc() - start;
    }
    tsc_period = delta / TSC_CALIBRATION_PERIOD;
    dprintf(6, "%s:%d: detected %lu.%luMHz TSC\n", __FILE__, __LINE__, delta / TSC_CALIBRATION_PERIOD, delta % TSC_CALIBRATION_PERIOD);
}

void tsc_sleep(size_t us) {
    uint64_t start = rdtsc();
    uint64_t target_ticks = us * tsc_period;

    while ((rdtsc() - start) < target_ticks) {
        asm volatile ("pause");
    }
}

uint64_t tsc_get_ticks(void) {
    return rdtsc();
}

void tsc_read_time(long *sec, long *nsec) {
    uint64_t total_nsec = tsc_get_ticks() / tsc_period;

    if (sec) *sec = total_nsec / 1000000ULL;
    if (nsec) *nsec = total_nsec % 1000000ULL;
}