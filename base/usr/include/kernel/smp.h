#pragma once
#include <stddef.h>
#include <kernel/list.h>

#define SMP_MAX_CORES   32

struct cpu {
    size_t id;
    size_t logical_id;
    
    list_t *processes;
    list_t *threads;
    node_t *current_tcb;
};

extern size_t cpu_count;

struct cpu *get_core(size_t core);
struct cpu *this_core(void);

#define this_cpu this_core()