#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/smp.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <limine.h>

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

extern void _ap_trampoline();

void ap_startup(void) {
    vectors_install();
    mmu_switch_pm(kernel_pd);
    gicc_install();

    for (;;) asm ("wfi");
}

void smp_initialize(void) {
    if (args_contains("nosmp")) {
        dprintf(LOG_INFO, "\033[93msmp:\033[0m SMP disabled by command line\n");
        return;
    }
    if (madt_giccs < 2)
        return;

    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));

    for (size_t i = 0; i < madt_giccs; i++) {
        if (madt_gicc_list[i]->mpidr == mpidr)
            continue;

        struct cpu *core = kmalloc(sizeof(struct cpu));
        core->id = i;
        core->logical_id = madt_gicc_list[i]->mpidr & 0xff;
        cpu_list[core->logical_id] = core;

        smp_request.response->cpus[i]->goto_address = (limine_goto_address)_ap_trampoline;
    }

    dprintf(LOG_INFO, "\033[93msmp:\033[0m started %lu processor(s)\n", madt_giccs - 1);
}

struct cpu *get_core(size_t core) {
    return cpu_list[core];
}

struct cpu *this_core(void) {
    uint64_t mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r" (mpidr));
    return cpu_list[mpidr & 0xff];
}