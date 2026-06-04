#include <kernel/printf.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>

extern void sched_shutdown(void);
extern void elf64_shutdown_modules(void);

__attribute__((noreturn))
void reboot(void) {
    sched_shutdown();
    elf64_shutdown_modules();
    dprintf(LOG_DEBUG, "\033[93mpower:\033[0m bringing down the system!\n");
    acpi_reboot();
    __builtin_unreachable();
}

__attribute__((noreturn))
void shutdown(void) {
    sched_shutdown();
    elf64_shutdown_modules();
    dprintf(LOG_DEBUG, "\033[93mpower:\033[0m bringing down the system!\n");
    acpi_shutdown();
    __builtin_unreachable();
}