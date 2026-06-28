#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <kernel/ringbuffer.h>
#include <kernel/spinlock.h>
#include <kernel/acpi.h>
#include <kernel/list.h>
#include <kernel/irq.h>

#define SMP_MAX_CORES   32

struct cpu {
    size_t id;
    size_t logical_id;
    
    list_t *threads;
    node_t *current_tcb;
    node_t *idle_tcb;

    uint64_t idle_time;
    uint64_t total_time;
    uint64_t last_reset;

    ringbuffer_t *tlb_invl_rb;
    bool tlb_pending;
    int  current_irq;

    #ifdef __aarch64__
    struct madt_gicc *gicc;
    #endif
};

extern struct cpu *cpu_list[SMP_MAX_CORES];
extern size_t      cpu_count;

void        set_tcb(uintptr_t tcb);
struct cpu *get_core(size_t core);
struct cpu *get_core_logical(size_t logical_id);
uint32_t    get_logical_id(void);

struct cpu     *this_core(void);
struct thread  *this_tcb(void);
struct process *this_process(void);

#define this this_tcb()
#define this_cpu this_core()
#define this_proc this_process()