#include <stdbool.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/lfbvideo.h>
#include <kernel/version.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <limine.h>

#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/panic.h>

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

void arch_fatal(void) {
	for (;;) asm ("wfi");
}

const char *esr_ec_reasons[0x40] = {
    [0x00] = "Unknown reason",
    [0x01] = "Trapped WFI/WFE",
    [0x03] = "Trapped MCR/MRC (coproc instructions, aarch32)",
    [0x04] = "Trapped MCRR/MRRC (coproc, aarch32)",
    [0x05] = "Trapped MCR/MRC (cp15, aarch32)",
    [0x06] = "Trapped LDC/STC (cp15, aarch32)",
    [0x07] = "Access to SVE/Advanced SIMD/FP regs disabled",
    [0x0E] = "Illegal execution state",
    [0x11] = "SVC instruction in aarch32 state",
    [0x15] = "SVC instruction in aarch64 state",
    [0x20] = "Instruction Abort (lower EL, current translation regime)",
    [0x21] = "Instruction Abort (current EL)",
    [0x22] = "PC alignment fault",
    [0x24] = "Data Abort (lower EL, same translation regime)",
    [0x25] = "Data Abort (current EL)",
    [0x26] = "SP alignment fault",
    [0x28] = "Trapped floating-point exception (aarch32)",
    [0x2C] = "Trapped floating-point exception (aarch64)",
    [0x38] = "BRK instruction (aarch64)",
    [0x3C] = "Software breakpoint (aarch32 BKPT)",
    [0x3F] = "Implementation-defined exception",
};

extern void arch_do_backtrace(void);

void aarch64_fault_handler(struct registers *r) {
    uint64_t esr_el1, far_el1, elr_el1, spsr_el1;
    
    asm volatile("mrs %0, ESR_EL1" : "=r"(esr_el1));
    asm volatile("mrs %0, FAR_EL1" : "=r"(far_el1));
    asm volatile("mrs %0, ELR_EL1" : "=r"(elr_el1));
    asm volatile("mrs %0, SPSR_EL1" : "=r"(spsr_el1));

    uint64_t ec = (esr_el1 >> 26) & 0x3F;

    dprintf(LOG_EMERG, "EL1-EL1 fault: \033[91m%s\033[0m on CPU %d\n", esr_ec_reasons[ec], 0);
    dprintf(LOG_EMERG, "x0:  0x%p x1:  0x%p x2:  0x%p x3:  0x%p\n", r->x0,  r->x1,  r->x2,  r->x3);
    dprintf(LOG_EMERG, "x4:  0x%p x5:  0x%p x6:  0x%p x7:  0x%p\n", r->x4,  r->x5,  r->x6,  r->x7);
    dprintf(LOG_EMERG, "x8:  0x%p x9:  0x%p x10: 0x%p x11: 0x%p\n", r->x8,  r->x9,  r->x10, r->x11);
    dprintf(LOG_EMERG, "x12: 0x%p x13: 0x%p x14: 0x%p x15: 0x%p\n", r->x12, r->x13, r->x14, r->x15);
    dprintf(LOG_EMERG, "x16: 0x%p x17: 0x%p x18: 0x%p x19: 0x%p\n", r->x16, r->x17, r->x18, r->x19);
    dprintf(LOG_EMERG, "x20: 0x%p x21: 0x%p x22: 0x%p x23: 0x%p\n", r->x20, r->x21, r->x22, r->x23);
    dprintf(LOG_EMERG, "x24: 0x%p x25: 0x%p x26: 0x%p x27: 0x%p\n", r->x24, r->x25, r->x26, r->x27);
    dprintf(LOG_EMERG, "x28: 0x%p x29: 0x%p x30: 0x%p PC:  0x%p\n", r->x28, r->x29, r->x30, elr_el1);
    dprintf(LOG_EMERG, "ESR_EL1: 0x%p\n", esr_el1);
    dprintf(LOG_EMERG, "FAR_EL1: 0x%p\n", far_el1);
    dprintf(LOG_EMERG, "ELR_EL1: 0x%p\n", elr_el1);
    dprintf(LOG_EMERG, "SPSR_EL1: 0x%p\n", spsr_el1);
    arch_do_backtrace();

    arch_fatal();
}

void vectors_install(void) {
    extern char _evt[];
    asm volatile("msr VBAR_EL1, %0" :: "r"(&_evt));
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

    char *ptr = kmalloc(14);
    strcpy(ptr, "Hello, world!");
    dprintf(LOG_INFO, "%s\n", ptr);
    
    panic("Mrrrp");

    arch_fatal();
}