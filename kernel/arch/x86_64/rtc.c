#include <kernel/arch/x86_64/hpet.h>
#include <kernel/printf.h>

void arch_sleep(size_t ns) {
    if (hpet) hpet_sleep(ns);
}

void uptime(size_t *sec, size_t *nsec) {
    if (hpet) hpet_read_time(sec, nsec);
}

uint64_t now(void) {
    dprintf(LOG_DEBUG, "\033[93mrtc:\033[0m now() is a stub\n");
    return 0;
}