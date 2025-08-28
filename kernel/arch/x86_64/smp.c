#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/list.h>
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

size_t cpu_count = 1;

void ap_startup() {
    idt_reinstall();
    mmu_switch_pm(kernel_pd);
    gdt_flush();
    lapic_reinstall();
    
    for (;;) asm ("hlt");
}

void smp_initialize(void) {
    if (args_contains("nosmp")) {
        dprintf(LOG_INFO, "\033[93msmp:\033[0m SMP disabled by command line\n");
        return;
    }
    cpu_count = madt_lapics;

    uint32_t eax = 1, bspid, _;
    asm volatile("cpuid" : "=a"(eax), "=b"(bspid), "=c"(_), "=d"(_) : "a"(eax));
    bspid >>= 24;

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = kmalloc(sizeof(struct cpu));
        core->id = i;
        core->logical_id = madt_lapic_list[i]->id;
        core->processes = list_create();
        core->threads = list_create();
        core->current_tcb = NULL;
        cpu_list[i] = core;

        if (madt_lapic_list[i]->id != bspid)
            smp_request.response->cpus[i]->goto_address = (limine_goto_address)ap_startup;
    }

    if (cpu_count > 1)
        dprintf(LOG_INFO, "\033[93msmp:\033[0m started %lu processor(s)\n", cpu_count - 1);
}

struct cpu *get_core(size_t core) {
    return cpu_list[core];
}

struct cpu *this_core(void) {
    uint32_t eax = 1, bspid, _;
    asm volatile("cpuid" : "=a"(eax), "=b"(bspid), "=c"(_), "=d"(_) : "a"(eax));
    bspid >>= 24;
    return cpu_list[bspid];
}