#pragma once
#include <stdbool.h>
#include <stddef.h>
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

    void *tlb_va;
    spinlock_t tlb_lock;

    struct madt_gicc *gicc;
};

extern struct cpu *cpu_list[SMP_MAX_CORES];
extern size_t cpu_count;

struct cpu *get_core(size_t core);
struct cpu *this_core(void);

#define this_cpu this_core()