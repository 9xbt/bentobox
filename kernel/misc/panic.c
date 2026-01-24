#include <stdarg.h>
#include <kernel/printf.h>
#include <kernel/witty.h>
#include <kernel/mmu.h>

extern void arch_fatal(void);
extern void arch_fatal_prepare(void);
extern void arch_do_backtrace(void);

void __panic(char *file, int line, char *fmt, ...) {
    arch_fatal_prepare();

    va_list args;
    va_start(args, fmt);
    char buf[1024] = {-1};
    vsprintf(buf, fmt, args);
    va_end(args);

    dprintf(LOG_EMERG, "%s:%d: \033[91mKernel panic:\033[0m %s\n", file, line, buf);
    dprintf(LOG_EMERG, "// %s\n", witty());
    arch_do_backtrace();

    arch_fatal();
}