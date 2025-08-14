#include <stdarg.h>
#include <kernel/printf.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/ksym.h>
#include <kernel/mmu.h>

extern void arch_prepare_fatal(void);
extern void arch_fatal(void);

void __panic(char *file, int line, char *fmt, ...) {
    arch_prepare_fatal();
    
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {-1};
    vsprintf(buf, fmt, args);
    va_end(args);

    dprintf(LOG_EMERG, "%s:%d: Kernel panic: %s\n", file, line, buf);

    struct stackframe *frame_ptr = __builtin_frame_address(0);

    dprintf(LOG_EMERG, "%s:%d: traceback:\n", file, line);

    for (int i = 0; i < 8 && frame_ptr->rbp; i++) {
        dprintf(LOG_EMERG, "#%d  0x%p in %s\n", i, frame_ptr->rip, ksym_name(frame_ptr->rip));
        if (!mmu_get_physical(this_core()->pml4, (uintptr_t)frame_ptr->rbp)) break;
        frame_ptr = frame_ptr->rbp;
    }

    arch_fatal();
}