#pragma once
#include <stdint.h>

#define IA32_GS_BASE        0xC0000101
#define IA32_GS_KERNEL_MSR  0xC0000102
#define IA32_FS_BASE        0xC0000100
#define IA32_EFER           0xC0000080
#define IA32_STAR           0xC0000081
#define IA32_LSTAR          0xC0000082
#define IA32_CSTAR          0xC0000083
#define IA32_APIC_BASE      0x0000001B

uint64_t rdmsr(uint32_t msr);
void wrmsr(uint32_t msr, uint64_t val);
void write_kernel_gs(uint64_t value);
void write_gs(uint64_t value);
uint64_t read_kernel_gs(void);
uint64_t read_gs(void);
void write_fs(uint64_t value);
void user_initialize(void);