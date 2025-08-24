#pragma once
#include <stdint.h>

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

struct registers {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;
    uint64_t int_no;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

struct stackframe {
    struct stackframe *rbp;
    uint64_t rip;
} __attribute__((packed));

void idt_install(void);
void idt_reinstall(void);
void idt_set_entry(uint8_t index, uint64_t base, uint16_t selector, uint8_t type);
void irq_register(uint8_t vector, void *handler);
void irq_unregister(uint8_t vector);