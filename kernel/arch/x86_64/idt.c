#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/witty.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>

extern void arch_fatal_prepare(void);
extern void arch_fatal(void);
extern void arch_do_backtrace(void);

__attribute__((aligned(0x10)))
struct idt_entry idt_entries[256];
struct idtr idt_descriptor;
extern void *idt_int_table[];

void *irq_handlers[256];

const char *isr_errors[32] = {
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
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    dprintf(LOG_INFO, "\033[93midt:\033[0m loaded IDT with 256 entries\n");
}

void idt_reinstall(void) {
    asm volatile ("lidt %0" :: "m"(idt_descriptor));
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
    irq_handlers[vector] = handler;
}

void irq_unregister(uint8_t vector) {
    irq_handlers[vector] = (void *)0;
}

extern void user_copy_fail();

void isr_handler(struct registers *r) {
    if (r->int_no == 15 || r->int_no == 255) {
        return;
    }
    if (r->int_no == 2) {
        asm ("cli");
	    for (;;) asm ("hlt");
    }
    if (r->cs == 0x23)
        asm volatile ("swapgs");
    struct cpu *cpu = get_core_logical(get_logical_id());
    struct thread *tcb = cpu->current_tcb ? cpu->current_tcb->value : NULL;
    struct process *proc = tcb ? tcb->parent : NULL;

    cpu->current_irq = r->int_no;

    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r" (cr2));

    if (r->int_no == 14 && (r->error_code & 0x03) == 0x03 && cr2 < hhdm_offset) {
        uint64_t flags = mmu_get_flags(mmu_get_pm(), (void *)cr2);
        if (flags & PTE_COW) {
            void *old_pa = (void *)(mmu_get_physical(mmu_get_pm(), (void *)ALIGN_DOWN(cr2, PAGE_SIZE)));
            uint16_t *refcount = mmu_get_refcount(old_pa);
            
            if (refcount && __atomic_load_n(refcount, __ATOMIC_ACQUIRE) > 1) {
                __atomic_sub_fetch(refcount, 1, __ATOMIC_ACQ_REL);
                void *pa = mmu_alloc();
                memcpy(VIRTUAL_HHDM(pa), VIRTUAL_HHDM(old_pa), PAGE_SIZE);
                mmu_map(mmu_get_pm(), (void *)ALIGN_DOWN(cr2, PAGE_SIZE), pa, (flags & ~PTE_COW) | PTE_WRITABLE);
            } else {
                mmu_map(mmu_get_pm(), (void *)ALIGN_DOWN(cr2, PAGE_SIZE), old_pa, (flags & ~PTE_COW) | PTE_WRITABLE);
            }
            
            tlb_invalidate((void *)ALIGN_DOWN(cr2, PAGE_SIZE));
            cpu->current_irq = 0xff;
            if (r->cs == 0x23)
                asm volatile ("swapgs");
            return;
        }
    }

    if (r->int_no == 14 && tcb && tcb->doing_user_copy && cr2 < hhdm_offset) {
        tcb->user_copy_status = -EFAULT;
        r->rip = (uint64_t)user_copy_fail;
        cpu->current_irq = 0xff;
        if (r->cs == 0x23)
            asm volatile ("swapgs");
        return;
    }
    
    if (r->cs == 0x23) {
        switch (r->int_no) {
            case 6:
                signal_send(proc, SIGILL);
                break;
            case 14:
            default:
                signal_send(proc, SIGSEGV);
                break;
        }
        cpu->current_irq = 0xff;
        if (tcb->state == THREAD_RUNNING)
            tcb->state = THREAD_READY;
        if (r->cs == 0x23)
            asm volatile ("swapgs");
        asm volatile ("int $0x80");

        return;
    }

    arch_fatal_prepare();

    uint32_t eax = 1, bspid, _;
    asm volatile("cpuid" : "=a"(eax), "=b"(bspid), "=c"(_), "=d"(_) : "a"(eax));
    bspid >>= 24;

    dprintf(LOG_EMERG, "x86 Fault: \033[91m%s\033[0m on CPU %d\n", isr_errors[r->int_no], bspid);
    dprintf(LOG_EMERG, "// %s\n", witty());
    dprintf(LOG_EMERG, "rdi: 0x%p rsi: 0x%p rbp:    0x%p\n", r->rdi, r->rsi, r->rbp);
    dprintf(LOG_EMERG, "rsp: 0x%p rbx: 0x%p rdx:    0x%p\n", r->rsp, r->rbx, r->rdx);
    dprintf(LOG_EMERG, "rcx: 0x%p rax: 0x%p rip:    0x%p\n", r->rcx, r->rax, r->rip);
    dprintf(LOG_EMERG, "r8:  0x%p r9:  0x%p r10:    0x%p\n", r->r8, r->r9, r->r10);
    dprintf(LOG_EMERG, "r11: 0x%p r12: 0x%p r13:    0x%p\n", r->r11, r->r12, r->r13);
    dprintf(LOG_EMERG, "r14: 0x%p r15: 0x%p cr2:    0x%p\n", r->r14, r->r15, cr2);
    dprintf(LOG_EMERG, "cs:  0x%p ss:  0x%p rflags: 0x%p\n", r->cs, r->ss, r->rflags);
    dprintf(LOG_EMERG, "tcb: 0x%p gs:  0x%p usergs: 0x%p\n", read_tcb(), read_kernel_gs(), read_gs());
    if (r->int_no == 14) {
        dprintf(LOG_EMERG, "%s %s %s\n",
            r->error_code & 0x01 ? "Page-protection violation," : "Page not present,",
            r->error_code & 0x02 ? "write operation," : "read operation,",
            r->error_code & 0x04 ? "user mode" : "kernel mode");
    }
    arch_do_backtrace();

    arch_fatal();
}

void irq_handler(struct registers *r) {
    if (r->cs == 0x23)
        asm volatile ("swapgs");

    struct cpu *cpu = get_core_logical(get_logical_id());
    __atomic_store_n(&cpu->current_irq, r->int_no, __ATOMIC_SEQ_CST);

    void(*handler)(struct registers *) = irq_handlers[r->int_no - 32];
    if (handler != NULL)
        handler(r);

    __atomic_store_n(&cpu->current_irq, 0xff, __ATOMIC_SEQ_CST);
    if (r->cs == 0x23)
        asm volatile ("swapgs");
}