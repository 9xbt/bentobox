#include <stdbool.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/mmu.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/lfbvideo.h>
#include <kernel/version.h>
#include <kernel/context.h>
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

__attribute__((used, section(".limine_requests")))
struct limine_executable_file_request ksym_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST,
    .revision = 0
};

extern void generic_startup(void);
extern void generic_main(void);

void arch_fatal_prepare(void) {
    static bool lock = false;

    if (!lock) {
        lock = true;
        for (size_t i = 0; i < cpu_count; i++) {
            struct cpu *core = get_core(i);
            if (core != this_cpu)
                lapic_ipi(core->logical_id, 0x02);
        }
    } else {
        asm ("cli");
	    for (;;) asm ("hlt");
    }
}

void arch_fatal(void) {
	asm ("cli");
	for (;;) asm ("hlt");
}

void arch_do_backtrace(void) {
    struct stackframe {
        struct stackframe *rbp;
        uint64_t rip;
    } __attribute__((packed)) *frame_ptr = __builtin_frame_address(0);

    dprintf(LOG_EMERG, "Call Trace:\n");

    for (int i = 0; i < 8 && frame_ptr->rbp && mmu_get_flags(mmu_get_pm(), frame_ptr) & PTE_PRESENT; i++) {
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->rip, ksym_name(frame_ptr->rip));
        frame_ptr = frame_ptr->rbp;
    }
}

void arch_context_init(struct thread *tcb, void *entry, bool user, void *stack) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    memset(ctx->fxsave, 0, sizeof(ctx->fxsave));
    
    ctx->stack_bottom = (uint64_t)kmalloc(SCHED_KERNEL_STACK_SIZE);
    ctx->stack = ctx->stack_bottom + (SCHED_KERNEL_STACK_SIZE) - 8;
    if (user) {
        ctx->user_stack_bottom = stack ? 0 : (uint64_t)vmalloc(tcb->parent->vma, tcb->parent->pm, 0, 0, SCHED_USER_STACK_PAGES, PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
        ctx->user_stack = (uint64_t)stack ?: ctx->user_stack_bottom + (SCHED_USER_STACK_SIZE);
    }
    ctx->regs.rsp = ctx->user_stack ?: ctx->stack;
    ctx->regs.rip = (uint64_t)entry;
    ctx->regs.cs = user ? 0x23 : 0x08;
    ctx->regs.ss = user ? 0x1b : 0x10;
    ctx->regs.rflags = 0x202;

    asm volatile("fninit" ::: "memory");
    asm volatile("fxsave %0" : "=m"(*ctx->fxsave) :: "memory");
}

void arch_context_free(struct thread *tcb) {
    struct context *ctx = &tcb->ctx;
    kfree((void *)ctx->stack_bottom);
}

void arch_context_fork(struct thread *tcb) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    memcpy(ctx, &this->ctx, 7 * sizeof(uint64_t));
    memcpy(&ctx->regs, this->syscall_regs, 15 * sizeof(uint64_t));
    asm volatile ("fxsave %0" :: "m"(tcb->ctx.fxsave));

    ctx->stack_bottom = (uint64_t)kmalloc(SCHED_KERNEL_STACK_SIZE);
    ctx->stack = ctx->stack_bottom + (SCHED_KERNEL_STACK_SIZE) - 8;
    memcpy((void *)ctx->stack_bottom, (void *)this->ctx.stack_bottom, SCHED_KERNEL_STACK_SIZE);

    ctx->regs.rsp = this->ctx.user_stack;
    ctx->regs.rip = ctx->regs.rcx;
    ctx->regs.rax = 0;
    ctx->regs.cs = 0x23;
    ctx->regs.ss = 0x1b;
    ctx->regs.rflags = 0x202;
}

void arch_setup_signal_frame(struct thread *tcb, struct sigframe *frame, struct sigaction *action, int sig) {
    struct context *ctx = &frame->ctx;
    memset(ctx, 0, sizeof(struct context));
    memcpy(ctx, &tcb->ctx, 7 * sizeof(uint64_t));
    memcpy(&ctx->regs, this->syscall_regs, 15 * sizeof(uint64_t));
    asm volatile ("fxsave %0" :: "m"(ctx->fxsave));
        
    tcb->ctx.regs.rip = (uint64_t)action->sa_handler;
    tcb->ctx.regs.rdi = sig;
    tcb->ctx.regs.cs = 0x23;
    tcb->ctx.regs.ss = 0x1b;
    tcb->ctx.regs.rflags = 0x202;
    
    uintptr_t rsp = (uintptr_t)frame;
    rsp -= 8;
    *(unsigned long *)rsp = frame->pretcode;
    
    tcb->ctx.regs.rsp = rsp;
    tcb->ctx.user_stack = rsp;
}

long arch_restore_signal_context(struct thread *tcb, struct sigframe *frame) {
    memcpy(&tcb->ctx, &frame->ctx, 7 * sizeof(uint64_t));
    memcpy(tcb->syscall_regs, &frame->ctx.regs, 15 * sizeof(uint64_t));
    asm volatile ("fxrstor %0" :: "m"(frame->ctx.fxsave));
    return frame->ctx.regs.rax;
}

void arch_save_context(void) {
    this->ctx.gs = read_kernel_gs();
    this->ctx.user_gs = read_gs();
    asm volatile ("fxsave %0" :: "m"(this->ctx.fxsave));
}

void arch_restore_context(void) {
    mmu_switch_pm(this_proc->pm);
    write_kernel_gs((uint64_t)this);
    write_gs(this->ctx.user_gs);
    set_kernel_stack(this->ctx.stack);
    asm volatile ("fxrstor %0" :: "m"(this->ctx.fxsave));
    write_fs(this->ctx.fs);
    lapic_eoi();
    lapic_oneshot(0x80, 5);
}

void arch_yield(struct cpu *cpu) {
    if (!cpu)
        return;
    if (cpu == this_cpu)
        asm volatile ("int $0x80");
    else
        lapic_ipi(cpu->logical_id, 0x80);
}

void arch_set_tls(uint64_t base) {
    write_fs(base);
    this->ctx.fs = base;
}

void arch_jumpstart(void) {
    irq_register(0x80 - 32, sched_schedule);
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        if (core != this_cpu)
            lapic_ipi(core->logical_id, 0x80);
    }
    lapic_ipi(this_cpu->logical_id, 0x80);
}

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        arch_fatal();
    }

    early_log_initialize();
    serial_install();

    dprintf(LOG_INFO, "%s %d.%d.%d %s %s %s %s\n",
        __kernel_name, __kernel_version_major, __kernel_version_minor, __kernel_version_patch,
		__kernel_commit_hash, __kernel_build_date, __kernel_build_time, __kernel_arch);

    gdt_install();
    idt_install();
    tss_install();
    mmu_initialize();
    framebuffer_initialize();
    elf64_module(ksym_request.response->executable_file);
    acpi_install();
    lapic_install();
    ioapic_install();
    smp_initialize();
    user_initialize();

    generic_startup();
    
    serial_initialize();
    ps2_hid_install();

    generic_main();    
}