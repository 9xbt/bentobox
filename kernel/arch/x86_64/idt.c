#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/ksym.h>
#include <kernel/elf64.h>
#include <kernel/printf.h>
#include <kernel/assert.h>

extern void arch_fatal(void);
extern void arch_prepare_fatal(void);

__attribute__((aligned(0x10)))
struct idt_entry idt_entries[256];
struct idtr idt_descriptor;
extern void *idt_int_table[];

void *irq_handlers[256];

const char* isr_errors[32] = {
    "division by zero",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "detected overflow",
    "out-of-bounds",
    "invalid opcode",
    "no coprocessor",
    "double fault",
    "coprocessor segment overrun",
    "bad TSS",
    "segment not present",
    "stack fault",
    "general protection fault",
    "page fault",
    "unknown interrupt",
    "coprocessor fault",
    "alignment check",
    "machine check",
    "SIMD floating-point exception",
    "virtualization exception",
    "control protection exception",
    "reserved",
    "hypervisor injection exception",
    "VMM communication exception",
    "security exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved"
};

void idt_install(void) {
    for (uint16_t i = 0; i < 256; i++) {
        idt_set_entry(i, (uint64_t)idt_int_table[i], 0x08, 0x8E);
    }

    idt_descriptor = (struct idtr) {
        .size = sizeof(struct idt_entry) * 256 - 1,
        .offset = (uint64_t)idt_entries
    };

    asm volatile ("lidt %0" :: "m"(idt_descriptor));
    dprintf("%s:%d: IDT address: 0x%p\n", __FILE__, __LINE__, (uint64_t)&idt_descriptor);
}

void idt_reinstall(void) {
    asm volatile ("lidt %0" :: "m"(idt_descriptor));
    asm volatile ("sti");
}

void idt_set_entry(uint8_t index, uint64_t base, uint16_t selector, uint8_t type) {
    idt_entries[index].base_low = base & 0xFFFF;
    idt_entries[index].selector = selector;
    idt_entries[index].zero = 0x00;
    idt_entries[index].type = type;
    idt_entries[index].base_mid = (base >> 16) & 0xFFFF;
    idt_entries[index].base_high = (base >> 32) & 0xFFFFFFFF;
    idt_entries[index].resv = 0;
}

void irq_register(uint8_t vector, void *handler) {
    if (ioapic_enabled && vector <= 15)
        ioapic_redirect_irq(0, vector + 32, vector, false);
    irq_handlers[vector] = handler;
}

void irq_unregister(uint8_t vector) {
    irq_handlers[vector] = (void *)0;
}

void isr_handler(struct registers *r) {
    if (r->int_no == 0xff) {
        return;
    }
    if (r->int_no == 2) {
        arch_fatal();
    }
    if (!this || !this->user) {
        arch_prepare_fatal();
    }

    static volatile bool faulted = false;
    if (faulted) {
        goto kill;
    }
    faulted = true;

    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r" (cr2));

    uint8_t bspid;
    asm volatile ("mov $1, %%eax; cpuid; shrl $24, %%ebx;": "=b"(bspid) : :);

    dprintf("%s:%d: x86 Fault: \033[91m%s\033[0m on CPU %d\n"
            "rdi: 0x%p rsi: 0x%p rbp:    0x%p\n"
            "rsp: 0x%p rbx: 0x%p rdx:    0x%p\n"
            "rcx: 0x%p rax: 0x%p rip:    0x%p\n"
            "r8:  0x%p r9:  0x%p r10:    0x%p\n"
            "r11: 0x%p r12: 0x%p r13:    0x%p\n"
            "r14: 0x%p r15: 0x%p cr2:    0x%p\n"
            "cs:  0x%p ss:  0x%p rflags: 0x%p\n",
            __FILE__, __LINE__, isr_errors[r->int_no], bspid, r->rdi, r->rsi, r->rbp,
            r->rsp, r->rbx, r->rdx, r->rcx, r->rax, r->rip, r->r8, r->r9,
            r->r10, r->r11, r->r12, r->r13, r->r14, r->r15, cr2, r->cs, r->ss,
            r->rflags);
    if (r->int_no == 14) {
        dprintf("%s:%d: %s %s %s\n", __FILE__, __LINE__,
            r->error_code & 0x01 ? "Page-protection violation," : "Page not present,",
            r->error_code & 0x02 ? "write operation," : "read operation,",
            r->error_code & 0x04 ? "user mode" : "kernel mode");
    }

    struct stackframe *frame_ptr = __builtin_frame_address(0);

    dprintf("%s:%d: traceback:\n", __FILE__, __LINE__);

    char symbol[256];
    for (int i = 0; i < 10 && frame_ptr->rbp; i++) {
        if (i == 0) {
            elf_symbol_name(symbol, ksymtab, kstrtab, ksym_count, r->rip);
            dprintf("#%d  0x%p in %s\n", i, r->rip, symbol);
            frame_ptr = frame_ptr->rbp;
            continue;
        }
        elf_symbol_name(symbol, ksymtab, kstrtab, ksym_count, frame_ptr->rip);
        dprintf("#%d  0x%p in %s\n", i, frame_ptr->rip, symbol);
        frame_ptr = frame_ptr->rbp;
    }
    faulted = false;

kill:
    if (this && this->user) {
        fprintf(1, "Segmentation fault (core dumped)\n", __FILE__, __LINE__, this->pid);
        asm volatile ("sti");
        sched_kill(this, 11);
    }

    arch_fatal();
}

void irq_handler(struct registers r) {
    void(*handler)(struct registers *);
    handler = irq_handlers[r.int_no - 32];

    if (handler != NULL)
        handler(&r);
}