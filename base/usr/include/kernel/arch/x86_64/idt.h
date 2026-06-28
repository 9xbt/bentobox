#pragma once
#include <stdint.h>
#include <kernel/smp.h>

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t resv;
} __attribute__((packed));

struct idtr {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed));

struct stackframe {
    struct stackframe *rbp;
    uint64_t rip;
} __attribute__((packed));

void idt_install(void);
void idt_reinstall(void);
void idt_set_entry(uint8_t index, uint64_t base, uint16_t selector, uint8_t type);