#include <stdint.h>
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
#include <kernel/irq.h>

const char *esr_ec_reasons[0x40] = {
    [0b000000] = "Unknown reason",
    [0b000001] = "Trapped WFI/WFE/WFET/WFIT",
    [0b000011] = "Trapped MCR/MRC access (coproc)",
    [0b000100] = "Trapped MCRR/MRRC (coproc)",
    [0b000101] = "Trapped MCR/MRC (coproc, aarch32)",
    [0b000110] = "Trapped LDC/STC (aarch32)",
    [0b000111] = "Access to SVE/Advanced SIMD/FP functionality",
    [0b001000] = "Trapped VMRS access",
    [0b001001] = "Trapped FEAT_PAuth",
    [0b001010] = "Trapped LD64B/ST64B/ST64BV/ST64BV0",
    [0b001100] = "Trapped MRRC access (coproc, aarch32)",
    [0b001110] = "Illegal execution state",
    [0b010001] = "SVC instruction (aarch32)",
    [0b010010] = "HVC instruction when not disabled (aarch32)",
    [0b010011] = "SMC instruction when not disabled (aarch32)",
    [0b010101] = "SVC instruction (aarch64)",
    [0b010110] = "HVC instruction when not disabled (aarch64)",
    [0b010111] = "SMC instruction when not disabled (aarch64)",
    [0b011000] = "Trapped MSR/MRS/System instruction not reported using EC 0x00/0x01/0x07",
    [0b011001] = "Trapped SVE access not reported using EC 0x00",
    [0b011010] = "Trapped ERET/ERETAA/ERETAB",
    [0b011100] = "Pointer authentication instruction failiure",
    [0b011111] = "Implementation defined",
    [0b100000] = "Instruction Abort (lower EL)",
    [0b100001] = "Instruction Abort (current EL)",
    [0b100010] = "PC alignment fault",
    [0b100100] = "Data Abort (lower EL)",
    [0b100101] = "Data Abort (current EL)",
    [0b100110] = "SP alignment fault",
    [0b101000] = "Trapped floating-point exception (aarch32)",
    [0b101100] = "Trapped floating-point exception (aarch64)",
    [0b101111] = "SError interrupt",
    [0b110000] = "Breakpoint exception (lower EL)",
    [0b110001] = "Breakpoint exception (current EL)",
    [0b110010] = "Software Step (lower EL)",
    [0b110011] = "Software Step (current EL)",
    [0b110100] = "Watchpoint exception (lower EL)",
    [0b110101] = "Watchpoint exception (current EL)",
    [0b111000] = "BKPT instruction (aarch32)",
    [0b111010] = "Vector Catch (aarch32)",
    [0b111100] = "BRK instruction (aarch64)"
};

extern void aarch64_backtrace(void *x29);
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
    aarch64_backtrace((void *)r->x29);
}

int do_cow(void) {
    uint64_t far_el1;
    asm volatile("mrs %0, FAR_EL1" : "=r"(far_el1));

    uint64_t flags = mmu_get_flags(mmu_get_pm(), (void *)far_el1);
    if (flags & PTE_COW) {
        void *old_pa = (void *)(mmu_get_physical(mmu_get_pm(), (void *)ALIGN_DOWN(far_el1, PAGE_SIZE)));
        uint16_t *refcount = mmu_get_refcount(old_pa);
        
        if (refcount && __atomic_load_n(refcount, __ATOMIC_ACQUIRE) > 1) {
            __atomic_sub_fetch(refcount, 1, __ATOMIC_ACQ_REL);
            void *pa = mmu_alloc();
            memcpy(VIRTUAL_HHDM(pa), VIRTUAL_HHDM(old_pa), PAGE_SIZE);
            mmu_map(mmu_get_pm(), (void *)ALIGN_DOWN(far_el1, PAGE_SIZE), pa, (flags & ~((0b11UL << 6) | PTE_COW)) | PTE_USER_RW);
        } else {
            mmu_map(mmu_get_pm(), (void *)ALIGN_DOWN(far_el1, PAGE_SIZE), old_pa, (flags & ~((0b11UL << 6) | PTE_COW)) | PTE_USER_RW);
        }
        
        tlb_invalidate((void *)ALIGN_DOWN(far_el1, PAGE_SIZE));
        return 0;
    }
    return 1;
}

extern void user_copy_fail();

void el1_fault_handler(struct registers *r) {
    uint64_t esr_el1, far_el1;
    asm volatile("mrs %0, ESR_EL1" : "=r"(esr_el1));
    asm volatile("mrs %0, FAR_EL1" : "=r"(far_el1));

    uint64_t ec = (esr_el1 >> 26) & 0x3F;
    this_cpu->current_irq = ec;
    if (ec == 0x25 && (esr_el1 & (1 << 6)) && ((esr_el1 & 0x3C) == 0x0C) && !do_cow()) {
        this_cpu->current_irq = 0xff;
        return;
    }

    if ((ec == 0x24 || ec == 0x25) && this && this->doing_user_copy && far_el1 < hhdm_offset) {
        this->user_copy_status = -EFAULT;
        asm volatile("msr ELR_EL1, %0" :: "r"((uint64_t)user_copy_fail));
        return;
    }

    do_regdump("EL1-EL1 fault", r);
    arch_fatal();
}

void el0_fault_handler(struct registers *r) {
    uint64_t esr_el1;
    asm volatile("mrs %0, ESR_EL1" : "=r"(esr_el1));
    
    uint64_t ec = (esr_el1 >> 26) & 0x3F;
    if (ec == 0x15) {
        asm volatile("mrs %0, ELR_EL1" : "=r"(this->ctx.elr_el0));
        asm volatile("mrs %0, SPSR_EL1" : "=r"(this->ctx.spsr_el0));
        this->syscall_regs = r;

        size_t args[] = { r->x8, r->x0, r->x1, r->x2, r->x3, r->x4, r->x5 };
        asm ("msr daifclr, #2");
        r->x0 = syscall_handler(args);
        signal_check_pending(this);
        asm ("msr daifset, #2");

        this->syscall_regs = NULL;
        asm volatile("msr ELR_EL1, %0" :: "r"(this->ctx.elr_el0));
        asm volatile("msr SPSR_EL1, %0" :: "r"(this->ctx.spsr_el0));
        return;
    }

    this_cpu->current_irq = ec;
    if (ec == 0x24 && (esr_el1 & (1 << 6)) && ((esr_el1 & 0x3C) == 0x0C) && !do_cow()) {
        this_cpu->current_irq = 0xff;
        return;
    }

    signal_send(this_proc, SIGSEGV);
    this->syscall_regs = r;
    signal_check_pending(this);

    this_cpu->current_irq = 0xff;
    return;
}

extern irq_t **irq_handlers;

void irq_handler(struct registers *r) {
    uint32_t iar = gicc_read(this_cpu->gicc, GICC_IAR);
    uint32_t irq = iar & 0x3FF;
    if (irq == 1023)
        return;
    this_cpu->current_irq = irq;

    irq_t *entry = irq_handlers[irq];
    if (entry) {
        void(*handler)(struct irq *, struct registers *) = entry->handler;
        handler(entry, r);
    }

    gicc_write(this_cpu->gicc, GICC_EOIR, iar);
    this_cpu->current_irq = 0xff;
}

void vectors_install(void) {
    extern char _evt[];
    asm volatile("msr VBAR_EL1, %0" :: "r"(&_evt));
}