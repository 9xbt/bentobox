#include <stdbool.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/mmu.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/lfbvideo.h>
#include <kernel/version.h>
#include <kernel/context.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

extern void generic_startup(void);
extern void generic_main(void);

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

    for (int i = 0; i < 8 && frame_ptr->rbp && mmu_get_flags(kernel_pd, frame_ptr) & PTE_PRESENT; i++) {
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->rip, "(none)");
        frame_ptr = frame_ptr->rbp;
    }
}

void schedule(struct registers *r) {
    lapic_stop_timer();

    if (this_cpu->current_tcb) {
        if (this->state != THREAD_NEW) {
            memcpy(&(this->ctx.regs), r, sizeof(struct registers));
            asm volatile ("fxsave %0 " : : "m"(this->ctx.fxsave));
        } else {
            this->state = THREAD_RUNNING;
        }
    } else {
        this_cpu->current_tcb = this_cpu->threads->head;
    }

    if (this_cpu->current_tcb->next)
        this_cpu->current_tcb = this_cpu->current_tcb->next;
    else
        this_cpu->current_tcb = this_cpu->threads->head;

    memcpy(r, &(this->ctx.regs), sizeof(struct registers));
    asm volatile ("fxrstor %0 " : : "m"(this->ctx.fxsave));

    lapic_eoi();
    lapic_oneshot(0x80, 5);
}

void idle(void) {
    for (;;) {
        asm ("hlt");
    }
}

void jumpstart(void) {
    irq_register(0x80 - 32, schedule);

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        if (core == this_cpu)
            continue;
        sched_add_process(core, sched_new_process(idle, "idle"));
        lapic_ipi(core->logical_id, 0x80);
    }
    sched_add_process(this_cpu, sched_new_process(idle, "idle"));
    lapic_ipi(this_cpu->logical_id, 0x80);
}

void create_context(struct context *ctx, void *entry) {
    memset(ctx, 0, sizeof(struct context));
    memset(&ctx->regs, 0, sizeof(struct registers));
    memset(ctx->fxsave, 0, sizeof(ctx->fxsave));

    ctx->stack = (uint64_t)VIRTUAL_HHDM(mmu_alloc());
    ctx->regs.rsp = ctx->stack + PAGE_SIZE - 8;
    ctx->regs.rip = (uint64_t)entry;
    ctx->regs.cs = 0x08;
    ctx->regs.ss = 0x10;
    ctx->regs.rflags = 0x202;
}

void destroy_context(struct context *ctx) {
    mmu_free(PHYSICAL_HHDM(ctx->stack));
}

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        arch_fatal();
    }

    framebuffer_initialize();

    dprintf(LOG_INFO, "%s %d.%d.%d %s %s %s %s\n",
        __kernel_name, __kernel_version_major, __kernel_version_minor, __kernel_version_patch,
		__kernel_commit_hash, __kernel_build_date, __kernel_build_time, __kernel_arch);

    gdt_install();
    idt_install();
    mmu_initialize();
    acpi_install();
    hpet_install();
    lapic_install();
    ioapic_install();
    smp_initialize();

    generic_startup();
    generic_main();
}