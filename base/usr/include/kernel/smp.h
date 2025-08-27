#pragma once
#include <stddef.h>

#define SMP_MAX_CORES   32

struct cpu {
    size_t id;
    size_t logical_id;
};

struct cpu *get_core(size_t core);
struct cpu *this_core(void);

#define this_cpu this_core()