#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/smp.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <kernel/irq.h>
#include <limine.h>
#include <stddef.h>

__attribute__((used, section(".limine_requests")))
struct limine_mp_request smp_request = {
    .id = LIMINE_MP_REQUEST,
    .revision = 0
};

struct cpu bsp = {
    .id = 0,
    .logical_id = 0
};
struct cpu *cpu_list[SMP_MAX_CORES] = { &bsp };

size_t cpu_count = 1;

extern void _ap_trampoline();

void hcf() {
    asm ("msr daifset, #2");
    for (;;) asm ("wfi");
}

void ap_startup(void) {
    vectors_install();
    mmu_switch_pm(kernel_pd);
    gic_initialize();

    asm volatile ("msr daifclr, #2");
    for (;;) asm ("wfi");
}

void smp_bootstrap(void) {
    if (args_contains("nosmp")) {
        dprintf(LOG_INFO, "\033[93msmp:\033[0m SMP disabled by command line\n");
        return;
    }
    if (cpu_count == 1)
        return;

    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));

    irq_allocate(gic_domain, hcf, 1, 1);
    
    for (size_t i = 0; i < cpu_count; i++) {
        if (madt_gicc_list[i]->mpidr != mpidr)
            smp_request.response->cpus[i]->goto_address = (limine_goto_address)_ap_trampoline;
    }
    
    dprintf(LOG_INFO, "\033[93msmp:\033[0m started %lu processor(s)\n", cpu_count - 1);
}

void smp_initialize(void) {
    cpu_count = args_contains("nosmp") ? 1 : MIN(madt_giccs, SMP_MAX_CORES);

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = kmalloc(sizeof(struct cpu));
        core->id = i;
        core->logical_id = madt_gicc_list[i]->mpidr & 0xff;
        core->threads = list_create();
        core->current_tcb = NULL;
        core->idle_tcb = NULL;
        core->idle_time = core->total_time = core->last_reset = 0;
        core->tlb_invl_rb = ringbuffer_create(PAGE_SIZE);
        core->tlb_pending = false;
        core->current_irq = 0xff;
        core->gicc = NULL;
        core->irq_frame = NULL;
        cpu_list[core->logical_id] = core;
    }
}

void set_tcb(uintptr_t tcb) {
    (void)tcb;
}

struct cpu *get_core(size_t id) {
    for (size_t i = 0; i < SMP_MAX_CORES; i++) {
        if (cpu_list[i] && cpu_list[i]->id == id)
            return cpu_list[i];
    }
    return NULL;
}

struct cpu *get_core_logical(size_t core) {
    return cpu_list[core];
}

uint32_t get_logical_id(void) {
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));
    return mpidr & 0xff;
}

struct cpu *this_core(void) {
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));
    return cpu_list[mpidr & 0xff];
}

struct thread *this_tcb(void) {
    return this_core()->current_tcb->value;
}

struct process *this_process(void) {
    return this_tcb()->parent;
}