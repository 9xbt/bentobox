#pragma once
#include <stddef.h>

#define SMP_MAX_CORES   32

struct cpu {
    size_t id;
    size_t logical_id;
};