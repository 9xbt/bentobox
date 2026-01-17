#include <stddef.h>
#include <kernel/printf.h>
#include <kernel/witty.h>

extern void arch_do_backtrace(void);
extern void arch_fatal_prepare(void);
extern void arch_fatal(void);

void __assert_failed(const char *file, size_t line, const char *func, const char *cond) {
    arch_fatal_prepare();
    
    dprintf(LOG_EMERG, "%s:%d (%s): \033[91mAssertion failed:\033[0m %s\n", file, line, func, cond);
    dprintf(LOG_EMERG, "// %s\n", witty());
    arch_do_backtrace();

    arch_fatal();
}