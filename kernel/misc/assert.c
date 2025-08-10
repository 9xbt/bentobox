#include <stdint.h>
#include <kernel/printf.h>

extern void arch_prepare_fatal(void);
extern void arch_fatal(void);

void __assert_failed(const char *file, uint32_t line, const char *func, const char *cond) {
    arch_prepare_fatal();
    printf("%s:%d (%s) Assertion failed: %s\n", file, line, func, cond);
    arch_fatal();
}

void __stub(const char *file, uint32_t line, const char *func) {
    dprintf(6, "%s:%d: %s is a stub\n", file, line, func);
}