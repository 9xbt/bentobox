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
#include <kernel/printf.h>
#include <kernel/acpi.h>
#include <kernel/mmu.h>
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