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

extern void aarch64_save_fp(__uint128_t *);
extern void aarch64_restore_fp(__uint128_t *);

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
        //dprintf(LOG_INFO, "a\n");
        asm ("wfi");
    }
}

void arch_context_init(struct thread *tcb, void *entry, bool user, int argc, char *argv[], char *envp[]) {
    struct context *ctx = &tcb->ctx;
    memset(ctx, 0, sizeof(struct context));
    memset(&ctx->regs, 0, sizeof(struct registers));
    ctx->elr_elx = (uint64_t)entry;
    ctx->spsr_elx = user ? 0x0 : 0x345;

    int envc = 0;
    if (envp) for (; envp[envc]; envc++);

    ctx->stack_bottom = (uint64_t)kmalloc(4 * PAGE_SIZE);
    ctx->stack = ctx->stack_bottom + (4 * PAGE_SIZE) - 8;
    ctx->regs.x16 = ctx->stack;
    if (user) {
        uintptr_t *pm = mmu_get_pm();
        mmu_switch_pm(tcb->parent->pm);

        ctx->user_stack_bottom = (uint64_t)vmalloc(tcb->parent->vma, tcb->parent->pm, 0x7ffffffff000 - (4 * PAGE_SIZE), 0, 4, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN | PTE_USER);
        ctx->user_stack = 0x7ffffffff000;

        long depth = ((argc + envc) % 2 == 0) ? 24 : 16;

        uint64_t argv_ptrs[argc + 1];
        uint64_t env_ptrs[envc + 1];
        argv_ptrs[argc] = 0;
        env_ptrs[envc] = 0;

        int i = 0;
        for (i = 0; i < envc; i++) {
            depth += ALIGN_UP(strlen(envp[i]) + 1, 16);
            env_ptrs[i] = (uint64_t)(ctx->user_stack - depth);
            strcpy((char *)ctx->user_stack - depth, envp[i]);
        }
        for (i = 0; i < argc; i++) {
            depth += ALIGN_UP(strlen(argv[i]) + 1, 16);
            argv_ptrs[i] = (uint64_t)(ctx->user_stack - depth);
            strcpy((char *)ctx->user_stack - depth, argv[i]);
        }

        #define PUSH(x) (*(uint64_t *)(ctx->user_stack - (depth += 8)) = (x))

        PUSH(0);
        for (i = envc - 1; i >= 0; i--) {
            PUSH(env_ptrs[i]);
        }

        PUSH(0);
        for (i = argc - 1; i >= 0; i--) {
            PUSH(argv_ptrs[i]);
        }

        PUSH(argc);

        ctx->user_stack -= depth;

        mmu_switch_pm(pm);
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
    uint64_t fpsr, fpcr;
    asm volatile("mrs %0, fpsr" : "=r"(fpsr));
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    ctx->fpsr = (uint32_t)fpsr;
    ctx->fpcr = (uint32_t)fpcr;
    aarch64_save_fp(tcb->ctx.fp);

    ctx->stack_bottom = (uint64_t)kmalloc(4 * PAGE_SIZE);
    ctx->stack = ctx->stack_bottom + (4 * PAGE_SIZE) - 8;
    memcpy((void *)ctx->stack_bottom, (void *)this->ctx.stack_bottom, 4 * PAGE_SIZE);

    ctx->regs.x0 = 0;
    ctx->regs.x16 = ctx->stack;
}

void arch_save_context(void) {
    asm volatile("msr CNTP_CTL_EL0, %0" :: "r"(0));
    
    asm volatile("mrs %0, ELR_EL1" : "=r"(this->ctx.elr_elx));
    asm volatile("mrs %0, SPSR_EL1" : "=r"(this->ctx.spsr_elx));

    asm volatile("mrs %0, SP_EL0" : "=r"(this->ctx.user_stack));

    asm volatile("mrs %0, TPIDR_EL0" : "=r"(this->ctx.tpidr_el0));

    uint64_t fpsr, fpcr;
    asm volatile("mrs %0, fpsr" : "=r"(fpsr));
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));

    this->ctx.fpsr = (uint32_t)fpsr;
    this->ctx.fpcr = (uint32_t)fpcr;

    aarch64_save_fp(this->ctx.fp);
}

void arch_restore_context(void) {
    uint64_t fpsr = this->ctx.fpsr, fpcr = this->ctx.fpcr;
    asm volatile("msr fpsr, %0" :: "r"(fpsr));
    asm volatile("msr fpcr, %0" :: "r"(fpcr));

    aarch64_restore_fp(this->ctx.fp);
    mmu_switch_pm(this_proc->pm);

    asm volatile("msr TPIDR_EL0, %0" :: "r"(this->ctx.tpidr_el0));

    asm volatile("msr SP_EL0, %0" :: "r"(this->ctx.user_stack));

    asm volatile("msr ELR_EL1, %0" :: "r"(this->ctx.elr_elx));
    asm volatile("msr SPSR_EL1, %0" :: "r"(this->ctx.spsr_elx));

    asm volatile("msr CNTP_CTL_EL0, %0" :: "r"(1));

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
    node_t *idle_proc = sched_add_process(sched_new_process("idle angel", false));
    
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        struct thread *tcb = sched_new_thread(idle_proc->value, idle, 0, NULL, NULL);
        tcb->state = THREAD_PAUSED;
        core->idle_tcb = list_insert(core->threads, tcb);
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
    //spawn("/bin/main", 0, NULL, NULL);
    generic_main();
}