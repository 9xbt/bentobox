#include <stdint.h>
#include <kernel/arch/aarch64/virtio.h>
#include <kernel/arch/aarch64/pl011.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/sched.h>
#include <kernel/witty.h>
#include <kernel/errno.h>
#include <kernel/smp.h>

const char *esr_ec_reasons[0x40] = {
    [0x00] = "Unknown reason",
    [0x01] = "Trapped WFI/WFE",
    [0x03] = "Trapped MCR/MRC (coproc instructions, aarch32)",
    [0x04] = "Trapped MCRR/MRRC (coproc, aarch32)",
    [0x05] = "Trapped MCR/MRC (cp15, aarch32)",
    [0x06] = "Trapped LDC/STC (cp15, aarch32)",
    [0x07] = "Access to SVE/Advanced SIMD/FP regs disabled",
    [0x0E] = "Illegal execution state",
    [0x11] = "SVC instruction in aarch32 state",
    [0x15] = "SVC instruction in aarch64 state",
    [0x20] = "Instruction Abort (lower EL, current translation regime)",
    [0x21] = "Instruction Abort (current EL)",
    [0x22] = "PC alignment fault",
    [0x24] = "Data Abort (lower EL, same translation regime)",
    [0x25] = "Data Abort (current EL)",
    [0x26] = "SP alignment fault",
    [0x28] = "Trapped floating-point exception (aarch32)",
    [0x2C] = "Trapped floating-point exception (aarch64)",
    [0x38] = "BRK instruction (aarch64)",
    [0x3C] = "Software breakpoint (aarch32 BKPT)",
    [0x3F] = "Implementation-defined exception",
};

void (*irq_handlers[80])(struct registers *);

extern void arch_do_backtrace(void);
extern void arch_fatal(void);

void do_regdump(const char *msg, struct registers *r) {
    uint64_t esr_el1, far_el1, elr_el1, spsr_el1;
    
    asm volatile("mrs %0, ESR_EL1" : "=r"(esr_el1));
    asm volatile("mrs %0, FAR_EL1" : "=r"(far_el1));
    asm volatile("mrs %0, ELR_EL1" : "=r"(elr_el1));
    asm volatile("mrs %0, SPSR_EL1" : "=r"(spsr_el1));

    uint64_t ec = (esr_el1 >> 26) & 0x3F;

    dprintf(LOG_EMERG, "%s: \033[91m%s\033[0m on CPU %d\n", msg, esr_ec_reasons[ec], this_cpu->id);
    dprintf(LOG_EMERG, "// %s\n", witty());
    dprintf(LOG_EMERG, "x0:  0x%p x1:  0x%p x2:  0x%p x3:  0x%p\n", r->x0,  r->x1,  r->x2,  r->x3);
    dprintf(LOG_EMERG, "x4:  0x%p x5:  0x%p x6:  0x%p x7:  0x%p\n", r->x4,  r->x5,  r->x6,  r->x7);
    dprintf(LOG_EMERG, "x8:  0x%p x9:  0x%p x10: 0x%p x11: 0x%p\n", r->x8,  r->x9,  r->x10, r->x11);
    dprintf(LOG_EMERG, "x12: 0x%p x13: 0x%p x14: 0x%p x15: 0x%p\n", r->x12, r->x13, r->x14, r->x15);
    dprintf(LOG_EMERG, "x16: 0x%p x17: 0x%p x18: 0x%p x19: 0x%p\n", r->x16, r->x17, r->x18, r->x19);
    dprintf(LOG_EMERG, "x20: 0x%p x21: 0x%p x22: 0x%p x23: 0x%p\n", r->x20, r->x21, r->x22, r->x23);
    dprintf(LOG_EMERG, "x24: 0x%p x25: 0x%p x26: 0x%p x27: 0x%p\n", r->x24, r->x25, r->x26, r->x27);
    dprintf(LOG_EMERG, "x28: 0x%p x29: 0x%p x30: 0x%p PC:  0x%p\n", r->x28, r->x29, r->x30, elr_el1);
    dprintf(LOG_EMERG, "ESR_ELx:  0x%p\n", esr_el1);
    dprintf(LOG_EMERG, "FAR_ELx:  0x%p\n", far_el1);
    dprintf(LOG_EMERG, "SPSR_ELx: 0x%p\n", spsr_el1);
    arch_do_backtrace();
}

extern void user_copy_fail();

void el1_fault_handler(struct registers *r) {
    uint64_t esr_el1, far_el1;
    asm volatile("mrs %0, ESR_EL1" : "=r"(esr_el1));
    asm volatile("mrs %0, FAR_EL1" : "=r"(far_el1));

    // uint64_t ec = (esr_el1 >> 26) & 0x3F;
    // if ((ec == 0x24 || ec == 0x25) && this && this->doing_user_copy && far_el1 < hhdm_offset) {
    //     this->user_copy_status = -EFAULT;
    //     this->doing_user_copy = false;

    //     asm volatile("msr ELR_EL1, %0" :: "r"((uint64_t)user_copy_fail));
    //     return;
    // }

    do_regdump("EL1-EL1 fault", r);
    arch_fatal();
}

void el0_fault_handler(struct registers *r) {
    uint64_t esr_el1;
    asm volatile("mrs %0, ESR_EL1" : "=r"(esr_el1));
    
    asm volatile("mrs %0, ELR_EL1" : "=r"(this->ctx.elr_el0));
    asm volatile("mrs %0, SPSR_EL1" : "=r"(this->ctx.spsr_el0));
    
    uint64_t ec = (esr_el1 >> 26) & 0x3F;
    if (ec == 0x15) {
        this->syscall_regs = r;
        size_t args[] = { r->x8, r->x0, r->x1, r->x2, r->x3, r->x4, r->x5 };

        asm ("msr daifclr, #2");
        r->x0 = syscall_handler(args);
        asm ("msr daifset, #2");

        asm volatile("msr ELR_EL1, %0" :: "r"(this->ctx.elr_el0));
        asm volatile("msr SPSR_EL1, %0" :: "r"(this->ctx.spsr_el0));
        return;
    }

    uint64_t far_el1;
    asm volatile("mrs %0, FAR_EL1" : "=r"(far_el1));

    if ((ec == 0x24 || ec == 0x25) && (esr_el1 & (1<<6)) && ((esr_el1 & 0x3C) == 0x0C)) {
        uint64_t flags = mmu_get_flags(mmu_get_pm(), (void *)far_el1);
        if (flags & PTE_COW) {
            void *old_pa = (void *)(mmu_get_physical(mmu_get_pm(), (void *)ALIGN_DOWN(far_el1, PAGE_SIZE)));
            uint16_t *refcount = mmu_get_refcount(old_pa);
            
            if (refcount && *refcount > 1) {
                (*refcount)--;
                void *pa = mmu_alloc();
                memcpy(VIRTUAL_HHDM(pa), VIRTUAL_HHDM(old_pa), PAGE_SIZE);
                mmu_map(mmu_get_pm(), (void *)ALIGN_DOWN(far_el1, PAGE_SIZE), pa, (flags & ~((0b11UL << 6) | PTE_COW)) | PTE_USER_RW);
            } else {
                mmu_map(mmu_get_pm(), (void *)ALIGN_DOWN(far_el1, PAGE_SIZE), old_pa, (flags & ~((0b11UL << 6) | PTE_COW)) | PTE_USER_RW);
            }
            
            tlb_invalidate((void *)ALIGN_DOWN(far_el1, PAGE_SIZE));
            return;
        }
    }

    signal_send(this_proc, SIGSEGV);
    sched_yield();
    for (;;) {}

    do_regdump("EL0-EL1 fault", r);
    arch_fatal();
}

void irq_handler(struct registers *r) {
    uint32_t iar = gicc_read(this_cpu->gicc, GICC_IAR);
    uint32_t irq = iar & 0x3FF;
    this_cpu->current_irq = irq;

    void(*handler)(struct registers *) = irq < 80 ? irq_handlers[irq] : NULL;
    if (handler)
        handler(r);

    gicc_write(this_cpu->gicc, GICC_EOIR, iar);
}

void irq_register(uint8_t vector, void *handler) {
    assert(vector < 80);
    irq_handlers[vector] = handler;
}

void irq_unregister(uint8_t vector) {
    assert(vector < 80);
    irq_handlers[vector] = NULL;
}

void hcf(void) {
    asm ("msr daifset, #2");
    for (;;) asm ("wfi");
}

void vectors_install(void) {
    irq_register(1, hcf);
    irq_register(0, sched_schedule);
    irq_register(30, sched_schedule);
    irq_register(33, pl011_irq_handler);

    extern char _evt[];
    asm volatile("msr VBAR_EL1, %0" :: "r"(&_evt));
}