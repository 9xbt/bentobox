#include <stdint.h>
#include <kernel/arch/x86_64/mmu.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>

void arch_print_backtrace(void) {
    struct stackframe {
        struct stackframe *rbp;
        uint64_t rip;
    } __attribute__((packed)) *frame_ptr = __builtin_frame_address(0);

    dprintf(LOG_EMERG, "Call Trace:\n");

    for (int i = 0; i < 8 && frame_ptr->rbp && mmu_get_flags(kernel_pd, frame_ptr) & PTE_PRESENT; i++) {
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->rip, "(none)");
        frame_ptr = frame_ptr->rbp;
    }
}