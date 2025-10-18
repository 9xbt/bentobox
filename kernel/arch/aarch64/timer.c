#include <stddef.h>
#include <stdint.h>

void arch_sleep(size_t ns) {
    uint64_t cntpct_el0, cntfrq_el0;

    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(cntpct_el0));
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(cntfrq_el0));
    
    size_t end_ticks = cntpct_el0 + ns * cntfrq_el0 / 1000000000;

    do {
        asm volatile("mrs %0, CNTPCT_EL0" : "=r"(cntpct_el0));
        asm volatile("wfe");
    } while (cntpct_el0 < end_ticks);
}

void uptime(size_t *sec, size_t *nsec) {
    extern uint64_t boot_time;
    if (!boot_time) return;

    uint64_t cntpct_el0, cntfrq_el0;

    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(cntpct_el0));
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(cntfrq_el0));

    uint64_t ticks = cntpct_el0 - boot_time;

    if (sec) *sec = ticks / cntfrq_el0;
    if (nsec) *nsec = (ticks % cntfrq_el0) * 1000000000 / cntfrq_el0;
}

uint64_t now(void) {
    uint64_t sec = 0;
    uptime(&sec, NULL);
    return sec;
}

void arch_clock_init(void) {}