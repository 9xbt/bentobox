#include <stdarg.h>
#include <kernel/mmu.h>
#include <kernel/ksym.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/printf.h>

extern void arch_prepare_fatal(void);
extern void arch_fatal(void);

void __panic(char *file, int line, char *fmt, ...) {
    arch_prepare_fatal();
    
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {-1};
    vsprintf(buf, fmt, args);
    va_end(args);

    dprintf("%s:%d: Kernel panic: %s\n", file, line, buf);

    struct stackframe *frame_ptr = __builtin_frame_address(0);

    dprintf("%s:%d: traceback:\n", __FILE__, __LINE__);

    char symbol[256];
    for (int i = 0; i < 8 && frame_ptr->rbp; i++) {
        elf_symbol_name(symbol, ksymtab, kstrtab, ksym_count, frame_ptr->rip);
        dprintf("#%d  0x%p in %s\n", i, frame_ptr->rip, symbol);
        if (!mmu_get_physical(this_core()->pml4, (uintptr_t)frame_ptr->rbp)) break;
        frame_ptr = frame_ptr->rbp;
    }

    arch_fatal();
}