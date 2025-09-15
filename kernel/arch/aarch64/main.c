#include <stdbool.h>
#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/arch/aarch64/smp.h>
#include <kernel/lfbvideo.h>
#include <kernel/version.h>
#include <kernel/printf.h>
#include <kernel/string.h>
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

void arch_fatal(void) {
    gic_send_sgi(1, 0xff & ~(1 << this_cpu->logical_id));
    asm ("msr daifset, #2");
	for (;;) asm ("wfi");
}

void arch_do_backtrace(void) {
    struct stackframe {
        struct stackframe *fp;
        uint64_t lr;
    } __attribute__((packed)) *frame_ptr = __builtin_frame_address(0);

    dprintf(LOG_EMERG, "Call Trace:\n");

    for (int i = 0; i < 8 && frame_ptr && mmu_get_flags(kernel_pd, frame_ptr) & PTE_VALID; i++) {
        dprintf(LOG_EMERG, " #%d 0x%p in %s\n", i, frame_ptr->lr, ksym_name(frame_ptr->lr));
        frame_ptr = frame_ptr->fp;
    }
}

void idle(void) {
    for (;;) {
        dprintf(LOG_INFO, "a\n");
        asm ("wfi");
    }
}

void arch_context_init(struct thread *tcb, void *entry, bool user) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    memset(&ctx->regs, 0, sizeof(struct registers));
    ctx->elr_elx = (uint64_t)entry;
    ctx->spsr_elx = user ? 0x0 : 0x345;
    ctx->stack_bottom = (uint64_t)vmalloc(kernel_vma, kernel_pd, 4, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN);
    ctx->stack = ctx->stack_bottom + (PAGE_SIZE * 4) - 8;
    if (user) {
        ctx->user_stack_bottom = (uint64_t)vmalloc(kernel_vma, kernel_pd, 4, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN | PTE_USER);
        ctx->user_stack = ctx->user_stack_bottom + (PAGE_SIZE * 4) - 8;
        ctx->regs.sp = ctx->user_stack;
    } else {
        ctx->regs.sp = ctx->stack;
    }
}

void arch_context_free(struct thread *tcb) {
    (void)tcb;
}

void arch_save_context(void) {
    asm volatile("msr CNTP_CTL_EL0, %0" :: "r"(0));
    
    asm volatile("mrs %0, ELR_EL1" : "=r"(this->ctx.elr_elx));
    asm volatile("mrs %0, SPSR_EL1" : "=r"(this->ctx.spsr_elx));

    asm volatile("mrs %0, SP_EL0" : "=r"(this->ctx.user_stack));
}

void arch_restore_context(void) {
    mmu_switch_pm(this->parent->pm);

    asm volatile("msr SP_EL0, %0" :: "r"(this->ctx.user_stack));

    asm volatile("msr ELR_EL1, %0" :: "r"(this->ctx.elr_elx));
    asm volatile("msr SPSR_EL1, %0" :: "r"(this->ctx.spsr_elx));

    asm volatile("msr CNTP_CTL_EL0, %0" :: "r"(1));

    uint64_t cntfrq_el0;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(cntfrq_el0));
    asm volatile("msr CNTP_TVAL_EL0, %0" :: "r"(cntfrq_el0 / 1000 * 250));
}

void arch_jumpstart(void) {
    node_t *idle_proc = sched_add_process(sched_new_process("idle", false));
    
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        core->idle_tcb = list_insert(core->threads, sched_new_thread(idle_proc->value, idle));
    }
    gic_send_sgi(0, 0xff);
}

uint64_t boot_time = 0;

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        arch_fatal();
    }

    framebuffer_initialize();

    dprintf(LOG_INFO, "%s %d.%d.%d %s %s %s %s\n",
        __kernel_name, __kernel_version_major, __kernel_version_minor, __kernel_version_patch,
		__kernel_commit_hash, __kernel_build_date, __kernel_build_time, __kernel_arch);

    asm volatile("mrs %0, CNTPCT_EL0" : "=r"(boot_time));
    vectors_install();
    mmu_initialize();
    elf64_module(ksym_request.response->executable_file);
    acpi_install();
    smp_initialize();
    gic_install();
    smp_bootstrap();

    generic_startup();
    //spawn("/bin/hello", 0, NULL, NULL);
    generic_main();
}