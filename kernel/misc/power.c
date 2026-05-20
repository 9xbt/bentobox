#include <kernel/printf.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>

extern void sched_shutdown(void);
extern void elf64_shutdown_modules(void);

__attribute__((noreturn))
void reboot(void) {
    dputs(LOG_DEBUG, "\n");
    sched_shutdown();
    elf64_shutdown_modules();
    acpi_reboot();
    __builtin_unreachable();
}

__attribute__((noreturn))
void shutdown(void) {
    sched_shutdown();
    elf64_shutdown_modules();
    acpi_shutdown();
    __builtin_unreachable();
}