#include <kernel/arch/x86_64/hpet.h>

void arch_sleep(size_t ns) {
    if (hpet) hpet_sleep(ns);
}

void uptime(long *sec, long *nsec) {
    if (hpet) hpet_read_time(sec, nsec);
}