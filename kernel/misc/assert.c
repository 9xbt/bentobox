#include <stddef.h>
#include <kernel/printf.h>

extern void arch_do_backtrace(void);
extern void arch_fatal(void);

void __assert_failed(const char *file, size_t line, const char *func, const char *cond) {
    dprintf(LOG_EMERG, "%s:%d (%s): Assertion failed: %s\n", file, line, func, cond);
    arch_do_backtrace();
    arch_fatal();
}