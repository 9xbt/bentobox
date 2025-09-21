#include <stdbool.h>
#include <kernel/arch/x86_64/ioapic.h>
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

extern void enable_sse(void);

extern void generic_startup(void);
extern void generic_main(void);

void arch_fatal(void) {
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        if (core != this_cpu)
            lapic_ipi(core->logical_id, 0x02);
    }

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
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->rip, ksym_name(frame_ptr->rip));
        frame_ptr = frame_ptr->rbp;
    }
}

void idle(void) {
    for (;;) {
        // dprintf(LOG_INFO, "a\n");
        asm ("hlt");
    }
}

void arch_context_init(struct thread *tcb, void *entry, bool user) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    memset(&ctx->regs, 0, sizeof(struct registers));
    memset(ctx->fxsave, 0, sizeof(ctx->fxsave));
    
    int argc = 0;
    char *argv[] = { NULL };
    
    ctx->stack_bottom = (uint64_t)kmalloc(4 * PAGE_SIZE);
    ctx->stack = ctx->stack_bottom + (4 * PAGE_SIZE) - 8;
    if (user) {
        uintptr_t *pm = mmu_get_pm();
        asm volatile ("cli" ::: "memory");
        mmu_switch_pm(tcb->parent->pm);
        
        ctx->user_stack_bottom = (uint64_t)vmalloc(tcb->parent->vma, tcb->parent->pm, 0x7ffffffff000 - (4 * PAGE_SIZE), 4, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        ctx->user_stack = 0x7ffffffff000;

        long depth = ((argc) % 2 == 0) ? 24 : 16;

        uint64_t argv_ptrs[argc + 1];
        argv_ptrs[argc] = 0;

        int i = 0;
        for (i = 0; i < argc; i++) {
            depth += ALIGN_UP(strlen(argv[i]) + 1, 16);
            argv_ptrs[i] = (uint64_t)(ctx->user_stack - depth);
            strcpy((char *)ctx->user_stack - depth, argv[i]);
        }

        #define PUSH(x) (*(uint64_t *)(ctx->user_stack - (depth += 8)) = (x))

        PUSH(0);
        for (i = argc - 1; i >= 0; i--) {
            PUSH(argv_ptrs[i]);
        }

        PUSH(argc);

        ctx->user_stack -= depth;

        mmu_switch_pm(pm);
        asm volatile ("sti" ::: "memory");
    }
    ctx->regs.rsp = user ? ctx->user_stack : ctx->stack;
    ctx->regs.rip = (uint64_t)entry;
    ctx->regs.cs = user ? 0x23 : 0x08;
    ctx->regs.ss = user ? 0x1b : 0x10;
    ctx->regs.rflags = 0x202;
    uint32_t *mxcsr = (uint32_t *)(ctx->fxsave + 24);
    *mxcsr = 0x1920;
    *mxcsr |= 0x8040;
}

void arch_context_free(struct thread *tcb) {
    struct context *ctx = &tcb->ctx;
    kfree((void *)ctx->stack_bottom);
    vfree(tcb->parent->vma, tcb->parent->pm, (void *)ctx->user_stack_bottom, 4);
}

void arch_save_context(void) {
    this->ctx.gs = read_kernel_gs();
    this->ctx.user_gs = read_gs();
    asm volatile ("fxsave %0 " : : "m"(this->ctx.fxsave));
}

void arch_restore_context(void) {
    mmu_switch_pm(this_proc->pm);
    write_kernel_gs((uint64_t)this);
    write_gs(this->ctx.user_gs);
    set_kernel_stack(this->ctx.stack);
    asm volatile ("fxrstor %0 " : : "m"(this->ctx.fxsave));
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
    node_t *idle_proc = sched_add_process(sched_new_process("idle angel", false));
    
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        struct thread *tcb = sched_new_thread(idle_proc->value, idle);
        tcb->state = THREAD_PAUSED;
        core->idle_tcb = list_insert(core->threads, tcb);
        if (core != this_cpu)
            lapic_ipi(core->logical_id, 0x80);
    }
    lapic_ipi(this_cpu->logical_id, 0x80);
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
    tss_install();
    elf64_module(ksym_request.response->executable_file);
    acpi_install();
    hpet_install();
    lapic_install();
    ioapic_install();
    smp_initialize();
    user_initialize();

    generic_startup();
    
    ps2_hid_install();
    spawn("/bin/main", 0, NULL, NULL);

    generic_main();    
}