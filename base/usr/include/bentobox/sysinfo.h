#pragma once
#include <stddef.h>

struct bb_sysinfo {
    size_t totalram;
    size_t freeram;
    size_t nprocs_onln;
};