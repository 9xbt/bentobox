#include <stdbool.h>
#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/smp.h>
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
	for (;;) asm ("wfi");
}

void uptime(long *sec, long *nsec) {
    if (sec) *sec = 0;
    if (nsec) *nsec = 0;
}

void kmain(void) {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        arch_fatal();
    }

    framebuffer_initialize();

    dprintf(LOG_INFO, "%s %d.%d.%d %s %s %s %s\n",
        __kernel_name, __kernel_version_major, __kernel_version_minor, __kernel_version_patch,
		__kernel_commit_hash, __kernel_build_date, __kernel_build_time, __kernel_arch);

    vectors_install();
    mmu_initialize();
    acpi_install();
    smp_initialize();

    generic_startup();
    generic_main();
}