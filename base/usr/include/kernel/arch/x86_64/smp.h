#pragma once
#include <stdint.h>
#include <stdatomic.h>
#include <kernel/list.h>

#define SMP_MAX_CORES 32

struct cpu {
    uint64_t lapic_id;
    uintptr_t *pml4;

    list_t *processes;
    node_t *current_proc;
    node_t *cleaner_proc;
    node_t *idle_proc;
    list_t *terminated_processes;
    atomic_flag sched_lock;
    atomic_flag vmm_lock;
};

void smp_initialize(void);
struct cpu *get_core(int core);
struct cpu *this_core(void);