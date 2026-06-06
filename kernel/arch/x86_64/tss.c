#include <stdint.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>

struct tss_entry tss[SMP_MAX_CORES] __attribute__((aligned(0x10)));

void write_tss(int cpu) {
    gdt_set_entry(cpu + 5, sizeof(struct tss_entry), (uint64_t)&tss[cpu], 0x89, 0x20);
    
    memset(&tss[cpu], 0, sizeof(struct tss_entry));

    asm volatile ("ltr %0" : : "r"((uint16_t)(0x28 + cpu * 16)));
}

void tss_install(void) {
    write_tss(get_core_logical(get_logical_id())->id);
}

void set_kernel_stack(uint64_t stack) {
    tss[get_core_logical(get_logical_id())->id].rsp0 = stack;
}