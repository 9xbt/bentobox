#pragma once
#include <stdint.h>
#include <stdatomic.h>
#include <kernel/list.h>

#define SMP_MAX_CORES 32

struct cpu {
    // TODO: just use the LAPIC ID
    uint64_t id;
    uint64_t lapic_id;
    uintptr_t *pml4; // Rename to a more generic name?

    //struct process *processes;
    //struct process *current_proc;
    list_t *processes;
    node_t *current_proc;
    struct process *cleaner_proc;
    node_t *idle_proc;
    list_t *terminated_processes;
    atomic_flag sched_lock;
    atomic_flag vmm_lock;
};

void smp_initialize(void);
struct cpu *get_core(int core);
struct cpu *this_core(void);