#include <stdbool.h>
#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/pl011.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/arch/aarch64/smp.h>
#include <kernel/lfbvideo.h>
#include <kernel/version.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/malloc.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/ksym.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

extern void aarch64_save_fp(__uint128_t *);
extern void aarch64_restore_fp(__uint128_t *);

extern void generic_startup(void);
extern void generic_main(void);

void arch_fatal_prepare(void) {
    gic_send_sgi(1, 0xff & ~(1 << this_cpu->logical_id));
}

void arch_fatal(void) {
    asm ("msr daifset, #2");
	for (;;) asm ("wfi");
}

void aarch64_backtrace(void *x29) {
    struct stackframe {
        struct stackframe *fp;
        uint64_t lr;
    } __attribute__((packed)) *frame_ptr = x29;

    dprintf(LOG_EMERG, "Call Trace:\n");

    for (int i = 0; i < 8 && frame_ptr && mmu_get_flags(mmu_get_pm(), frame_ptr) & PTE_VALID; i++) {
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->lr, ksym_name(frame_ptr->lr));
        frame_ptr = frame_ptr->fp;
    }
}

void arch_do_backtrace(void) {
    aarch64_backtrace(__builtin_frame_address(0));
}

void arch_context_init(struct thread *tcb, void *entry, bool user, void *stack) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    memset(&ctx->regs, 0, sizeof(struct registers));
    ctx->elr_elx = (uint64_t)entry;
    ctx->spsr_elx = user ? 0x0 : 0x345;

    ctx->stack_bottom = (uint64_t)kmalloc(SCHED_KERNEL_STACK_SIZE);
    ctx->stack = ctx->stack_bottom + (SCHED_KERNEL_STACK_SIZE) - 8 - sizeof(struct registers);
    if (user) {
        ctx->user_stack_bottom = stack ? 0 : (uint64_t)vmalloc(tcb->parent->vma, tcb->parent->pm, 0, 0, SCHED_USER_STACK_PAGES, PTE_VALID | PTE_AF | PTE_USER_RW | PTE_PXN);
        ctx->user_stack = (uint64_t)stack ?: ctx->user_stack_bottom + (SCHED_USER_STACK_SIZE);
    } else {
        ctx->regs.x0 = (uint64_t)stack;
    }
}

void arch_context_free(struct thread *tcb) {
    struct context *ctx = &tcb->ctx;
    kfree((void *)ctx->stack_bottom);
}

void arch_context_fork(struct thread *tcb) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    asm volatile("mrs %0, SP_EL0" : "=r"(ctx->user_stack));
    ctx->user_stack_bottom = this->ctx.user_stack_bottom;
    memcpy(&ctx->regs, this->syscall_regs, sizeof ctx->regs);
    ctx->elr_elx = this->ctx.elr_el0;
    ctx->spsr_elx = this->ctx.spsr_el0;
    asm volatile("mrs %0, TPIDR_EL0" : "=r"(ctx->tpidr_el0));
    asm volatile("mrs %0, fpsr" : "=r"(ctx->fpsr));
    asm volatile("mrs %0, fpcr" : "=r"(ctx->fpcr));
    aarch64_save_fp(tcb->ctx.fp);

    ctx->stack_bottom = (uint64_t)kmalloc(SCHED_KERNEL_STACK_SIZE);
    ctx->stack = ctx->stack_bottom + (SCHED_KERNEL_STACK_SIZE) - 8 - sizeof(struct registers);
    memcpy((void *)ctx->stack_bottom, (void *)this->ctx.stack_bottom, SCHED_KERNEL_STACK_SIZE);

    ctx->regs.x0 = 0;
}

void arch_setup_signal_frame(struct thread *tcb, struct sigframe *frame, struct sigaction *action, int sig) {
    struct context *ctx = &frame->ctx;
    memcpy(ctx, &tcb->ctx, sizeof tcb->ctx);
    memcpy(&ctx->regs, tcb->syscall_regs, sizeof(struct registers));

    aarch64_save_fp(ctx->fp);
    // SP_EL0 already saved by el0_fault_handler
    asm volatile("mrs %0, fpsr" : "=r"(ctx->fpsr));
    asm volatile("mrs %0, fpcr" : "=r"(ctx->fpcr));

    uintptr_t sp = (uintptr_t)frame;
    sp &= ~15;
    asm volatile("msr SP_EL0, %0" :: "r"(sp));
    tcb->ctx.elr_el0 = (uint64_t)action->sa_handler;
    tcb->ctx.spsr_el0 = 0x0;
    tcb->syscall_regs->x0 = sig;
    tcb->syscall_regs->x30 = frame->pretcode;
}

long arch_restore_signal_context(struct thread *tcb, struct sigframe *frame) {
    memcpy(&tcb->ctx, &frame->ctx, sizeof tcb->ctx);
    memcpy(tcb->syscall_regs, &frame->ctx.regs, sizeof(struct registers));
    asm volatile("msr fpsr, %0" :: "r"(tcb->ctx.fpsr));
    asm volatile("msr fpcr, %0" :: "r"(tcb->ctx.fpcr));

    asm volatile("msr SP_EL0, %0" :: "r"(tcb->ctx.user_stack));
    aarch64_restore_fp(this->ctx.fp);

    this->ctx.elr_el0 = tcb->ctx.elr_el0;
    this->ctx.spsr_el0 = tcb->ctx.spsr_el0;

    return frame->ctx.regs.x0;
}

void arch_save_context(struct registers *r) {
    this->ctx.stack = (uint64_t)r;
    memcpy(&(this->ctx.regs), r, sizeof(struct registers));

    asm volatile("msr CNTP_CTL_EL0, %0" :: "r"((uint64_t)0));
    
    asm volatile("mrs %0, ELR_EL1" : "=r"(this->ctx.elr_elx));
    asm volatile("mrs %0, SPSR_EL1" : "=r"(this->ctx.spsr_elx));

    asm volatile("mrs %0, SP_EL0" : "=r"(this->ctx.user_stack));

    asm volatile("mrs %0, TPIDR_EL0" : "=r"(this->ctx.tpidr_el0));

    asm volatile("mrs %0, fpsr" : "=r"(this->ctx.fpsr));
    asm volatile("mrs %0, fpcr" : "=r"(this->ctx.fpcr));

    aarch64_save_fp(this->ctx.fp);
}

void arch_restore_context() {
    struct registers *r = (struct registers *)this->ctx.stack;
    memcpy(r, &(this->ctx.regs), sizeof(struct registers));
    this_cpu->irq_frame = r;

    asm volatile("msr fpsr, %0" :: "r"(this->ctx.fpsr));
    asm volatile("msr fpcr, %0" :: "r"(this->ctx.fpcr));

    aarch64_restore_fp(this->ctx.fp);
    mmu_switch_pm(this_proc->pm);

    asm volatile("msr TPIDR_EL0, %0" :: "r"(this->ctx.tpidr_el0));

    asm volatile("msr SP_EL0, %0" :: "r"(this->ctx.user_stack));

    asm volatile("msr ELR_EL1, %0" :: "r"(this->ctx.elr_elx));
    asm volatile("msr SPSR_EL1, %0" :: "r"(this->ctx.spsr_elx));

    asm volatile("msr CNTP_CTL_EL0, %0" :: "r"((uint64_t)1));

    uint64_t cntfrq_el0;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(cntfrq_el0));
    asm volatile("msr CNTP_TVAL_EL0, %0" :: "r"(cntfrq_el0 / 1000 * 5));
}

void arch_yield(struct cpu *cpu) {
    gic_send_sgi(0, (1 << cpu->logical_id));
}

void arch_set_tls(uint64_t base) {
    asm volatile("msr TPIDR_EL0, %0" :: "r"(base));
}

void arch_jumpstart(void) {
    gic_send_sgi(0, 0xff);
}

uint64_t boot_time = 0;

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        arch_fatal();
    }

    early_log_initialize();
    log_register_sink(uart_write);

    dprintf(LOG_INFO, "%s %d.%d.%d %s %s %s %s\n",
        __kernel_name, __kernel_version_major, __kernel_version_minor, __kernel_version_patch,
		__kernel_commit_hash, __kernel_build_date, __kernel_build_time, __kernel_arch);

    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(boot_time));

    vectors_install();
    mmu_initialize();
    pl011_install();
    framebuffer_initialize();
    ksym_install();
    acpi_install();
    smp_initialize();
    gic_install();
    smp_bootstrap();

    generic_startup();

    pl011_initialize();
    irq_allocate(gic_domain, sched_schedule, 0, 0);
    irq_allocate(gic_domain, sched_schedule, 30, 30);
    
    generic_main();
}