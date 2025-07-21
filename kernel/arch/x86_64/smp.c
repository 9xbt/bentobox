#include <stdatomic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/mmu.h>

extern void _L8000_ap_trampoline();

volatile uint8_t smp_running_cpus = 1;
static atomic_flag smp_init_lock = ATOMIC_FLAG_INIT;

struct cpu bsp = {
    .lapic_id = 0,
    .processes = NULL,
    .current_proc = NULL
};
struct cpu *smp_cpu_list[SMP_MAX_CORES] = { &bsp };

/*
 * https://wiki.osdev.org/Symmetric_Multiprocessing
 */
__attribute__((no_sanitize("undefined")))
void smp_initialize(void) {
    assert(madt_lapics > 0);
    if (madt_lapics == 1)
        return;
    if (args_contains("nosmp")) {
        dprintf("%s:%d: SMP disabled by kernel command line\n", __FILE__, __LINE__);
        madt_lapics = 1;
        return;
    }
    if (madt_lapics > SMP_MAX_CORES)
        panic("Too many cores! Please rebuild bentobox with SMP_MAX_CORES=%u", madt_lapics);

    uint8_t bspid;
    asm volatile ("mov $1, %%eax; cpuid; shrl $24, %%ebx;": "=b"(bspid) : :); /* get the BSP's LAPIC ID */

    memcpy((void*)0x8000, &_L8000_ap_trampoline, PAGE_SIZE);

    for (uint32_t i = 0; i < madt_lapics; i++) {
        if (madt_lapic_list[i]->id == bspid)
            continue; /* skip BSP, that's already running this code */
        
        acquire(&smp_init_lock);

        struct cpu *core = (struct cpu *)kmalloc(sizeof(struct cpu));
        core->lapic_id = madt_lapic_list[i]->id;
        core->processes = NULL;
        core->current_proc = NULL;
        core->terminated_processes = NULL;
        core->usage = 0;
        smp_cpu_list[i] = core;

        /* send INIT IPI */
        lapic_write(LAPIC_ESR, 0);                                                        /* clear APIC errors */
        lapic_write(LAPIC_ICRHI, i << LAPIC_ICDESTSHIFT);                                 /* select AP */
        lapic_write(LAPIC_ICRLO, (lapic_read(LAPIC_ICRLO) & 0xfff00000) | 0x00C500); /* trigger INIT IPI */
        do {
            asm volatile ("pause" : : : "memory");                                                   /* wait for delivery */
        } while (lapic_read(LAPIC_ICRLO) & (1 << 12));
        lapic_write(LAPIC_ICRHI, i << LAPIC_ICDESTSHIFT);                                 /* select AP */
        lapic_write(LAPIC_ICRLO, (lapic_read(LAPIC_ICRLO) & 0xfff00000) | 0x008500); /* deassert */
        do {
            asm volatile ("pause" : : : "memory");                                                   /* wait for delivery */
        } while (lapic_read(LAPIC_ICRLO) & (1 << 12));

        hpet_sleep(10000);

        /* send STARTUP IPI (twice) */
        for(int j = 0; j < 2; j++) {
            lapic_write(LAPIC_ESR, 0);                                                        /* clear APIC errors */
            lapic_write(LAPIC_ICRHI, i << LAPIC_ICDESTSHIFT);                                 /* select AP */
            lapic_write(LAPIC_ICRLO, (lapic_read(LAPIC_ICRLO) & 0xfff0f800) | 0x000608); /* trigger STARTUP IPI for 0x0800:0x0000 */
            hpet_sleep(200);
            do {
                asm volatile ("pause" : : : "memory");                                                   /* wait for delivery */
            } while (lapic_read(LAPIC_ICRLO) & (1 << 12));
        }

        acquire(&smp_init_lock);
        release(&smp_init_lock);
    }

    dprintf("%s:%d: started %d processors\n", __FILE__, __LINE__, smp_running_cpus);
    //printf("\033[92m * \033[97mInitialized SMP with %d CPU%s\033[0m\n", smp_running_cpus, smp_running_cpus == 1 ? "" : "s");

    /* TODO: return by jumping to __builtin_extract_return_addr(__builtin_return_address(0)) when compiling with UBSAN? */
}

struct cpu *get_core(int core) {
    return smp_cpu_list[core];
}

struct cpu *this_core(void) {
    uint8_t bspid;
    asm volatile ("mov $1, %%eax; cpuid; shrl $24, %%ebx;": "=b"(bspid) : :);
    return smp_cpu_list[bspid];
}

void ap_startup(void) {
    idt_reinstall();
    vmm_switch_pm(kernel_pd);
    gdt_flush();
    tss_install();
    lapic_install();
    lapic_calibrate_timer();
    lapic_eoi();
    user_initialize();

    //uint64_t id = this_core()->id;
    //printf("Hello from CPU %d!\n", id);
    
    smp_running_cpus++;
    release(&smp_init_lock);

	for (;;) asm ("hlt");
}