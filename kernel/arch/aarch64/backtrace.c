#include <stdint.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>

void arch_do_backtrace(void) {
    struct stackframe {
        struct stackframe *fp;
        uint64_t lr;
    } __attribute__((packed)) *frame_ptr = __builtin_frame_address(0);

    dprintf(LOG_EMERG, "Call Trace:\n");

    for (int i = 0; i < 8 && frame_ptr && mmu_get_flags(kernel_pd, frame_ptr) & PTE_VALID; i++) {
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->lr, "(none)");
        frame_ptr = frame_ptr->fp;
    }
}