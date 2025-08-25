#include <stdbool.h>
#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/lfbvideo.h>
#include <kernel/version.h>
#include <kernel/printf.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

void arch_fatal(void) {
	for (;;) asm ("wfi");
}

void aarch64_fault_handler(struct registers *r) {
    (void)r;
    dprintf(LOG_INFO, "Fault occurred!\n");

    arch_fatal();
}

void vectors_install(void) {
    extern char _evt[];
    asm volatile("msr VBAR_EL1, %0" :: "r"(&_evt));

    uint64_t stack;
    asm volatile("mov %0, sp" : "=r"(stack));
    asm volatile("msr spsel, #1");
    asm volatile("mov sp, %0" :: "r"(stack));

    dprintf(LOG_INFO, "%s:%d: installed exception vectors\n", __FILE__, __LINE__);
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

    asm volatile ("udf #0");

    arch_fatal();
}