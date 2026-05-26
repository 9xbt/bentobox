#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <kernel/ringbuffer.h>
#include <kernel/spinlock.h>
#include <kernel/acpi.h>
#include <kernel/list.h>

#define SMP_MAX_CORES   32

struct cpu {
    size_t id;
    size_t logical_id;
    
    list_t *threads;
    node_t *current_tcb;
    node_t *idle_tcb;
    struct thread *prev_tcb;

    uint64_t idle_time;
    uint64_t total_time;
    uint64_t last_reset;

    ringbuffer_t *tlb_invl_rb;
    bool tlb_pending;

    #ifdef __aarch64__
    struct madt_gicc *gicc;
    int current_irq;
    #endif
};

extern struct cpu *cpu_list[SMP_MAX_CORES];
extern size_t cpu_count;

struct cpu *get_core(size_t core);
struct cpu *this_core(void);

#define this_cpu this_core()