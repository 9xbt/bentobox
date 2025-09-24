#include <stdint.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/context.h>
#include <kernel/syscall.h>
#include <kernel/sched.h>

extern void syscall_entry(void);

uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile ("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

uint64_t read_kernel_gs(void) {
    return rdmsr(IA32_GS_KERNEL_MSR);
}

void write_kernel_gs(uint64_t value) {
    wrmsr(IA32_GS_KERNEL_MSR, value);
}

uint64_t read_gs(void) {
    return rdmsr(IA32_GS_BASE);
}

void write_gs(uint64_t value) {
    wrmsr(IA32_GS_BASE, value);
}

void write_fs(uint64_t value) {
    wrmsr(IA32_FS_BASE, value);
}

void user_initialize(void) {
    wrmsr(IA32_EFER, rdmsr(IA32_EFER) | (1 << 0) | (1ULL << 11));
    wrmsr(IA32_STAR, ((uint64_t)0x08 << 32) | ((uint64_t)0x13 << 48));
    wrmsr(IA32_LSTAR, (uint64_t)syscall_entry);
    wrmsr(IA32_CSTAR, 0);
    wrmsr(IA32_CSTAR + 1, 0x200);
}

void do_syscall(struct registers *r) {
    this->syscall_regs = r;
    size_t args[] = { r->rax, r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9 };
    r->rax = syscall_handler(args);
}

void enable_sse(void) {
    asm volatile (
        "mov %%cr0, %%rax\n"
        "and $0xFFFB, %%ax\n"
        "or $0x2, %%ax\n"
        "mov %%rax, %%cr0\n"
        "mov %%cr4, %%rax\n"
        "or $0x600, %%ax\n"
        "mov %%rax, %%cr4"
        ::: "rax", "memory"
    );
}