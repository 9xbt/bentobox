#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/ringbuffer.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/list.h>
#include <kernel/irq.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
struct limine_mp_request smp_request = {
    .id = LIMINE_MP_REQUEST,
    .revision = 0
};

struct cpu bsp = {
    .current_irq = 0
};

struct thread stub_thread = {
    .cpu = &bsp
};

struct cpu *cpu_list[SMP_MAX_CORES] = { &bsp };

size_t cpu_count = 1;

void ap_startup() {
    idt_reinstall();
    mmu_switch_pm(kernel_pd);
    gdt_flush();
    tss_install();
    lapic_reinstall();
    user_initialize();

    for (;;) asm ("hlt");
}

void smp_tlb_invalidate() {
    uint64_t va;
    while (ringbuffer_read(this_cpu->tlb_invl_rb, (unsigned char *)&va, sizeof va)) {
        asm volatile ("invlpg (%0)" ::"r"(va) : "memory");
    }
    __atomic_store_n(&this_cpu->tlb_pending, false, __ATOMIC_SEQ_CST);
}

void smp_bootstrap(void) {
    if (args_contains("nosmp")) {
        dprintf(LOG_INFO, "\033[93msmp:\033[0m SMP disabled by command line\n");
        return;
    }
    if (cpu_count == 1)
        return;

    uint32_t eax = 1, bspid, _;
    asm volatile("cpuid" : "=a"(eax), "=b"(bspid), "=c"(_), "=d"(_) : "a"(eax));
    bspid >>= 24;

    irq_allocate(lapic_domain, smp_tlb_invalidate, 0x81, 0x81);

    for (size_t i = 0; i < cpu_count; i++) {
        if (smp_request.response->cpus[i]->lapic_id != bspid)
            smp_request.response->cpus[i]->goto_address = (limine_goto_address)ap_startup;
    }

    dprintf(LOG_INFO, "\033[93msmp:\033[0m started %lu processor(s)\n", cpu_count - 1);    
}

void smp_initialize(void) {
    cpu_count = args_contains("nosmp") ? 1 : MIN(madt_lapics, SMP_MAX_CORES);

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = kmalloc(sizeof(struct cpu));
        core->id = i;
        core->logical_id = madt_lapic_list[i]->id;
        core->threads = list_create();
        core->current_tcb = NULL;
        core->idle_tcb = NULL;
        core->idle_time = core->total_time = core->last_reset = 0;
        core->tlb_invl_rb = ringbuffer_create(PAGE_SIZE);
        core->tlb_pending = false;
        core->current_irq = 0xff;
        cpu_list[core->logical_id] = core;
    }
    
    smp_bootstrap();
}

void set_tcb(uintptr_t tcb) {
    write_gs(tcb);
}

struct cpu *get_core(size_t id) {
    for (size_t i = 0; i < SMP_MAX_CORES; i++) {
        if (cpu_list[i] && cpu_list[i]->id == id)
            return cpu_list[i];
    }
    return NULL;
}

struct cpu *get_core_logical(size_t logical_id) {
    return cpu_list[logical_id];
}

uint32_t get_logical_id(void) {
    uint32_t eax = 1, bspid, _;
    asm volatile("cpuid" : "=a"(eax), "=b"(bspid), "=c"(_), "=d"(_) : "a"(eax));
    return bspid >> 24;
}

struct cpu *this_core(void) {
    struct cpu *core;
    asm volatile("movq %%gs:760, %0" : "=r"(core));
    return core;
}

struct thread *this_tcb(void) {
    struct thread *tcb;
    asm volatile("movq %%gs:768, %0" : "=r"(tcb));
    return tcb;
}

struct process *this_process(void) {
    struct process *proc;
    asm volatile("movq %%gs:752, %0" : "=r"(proc));
    return proc;
}