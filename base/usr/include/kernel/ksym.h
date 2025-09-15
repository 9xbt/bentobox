#pragma once
#include <stddef.h>
#include <kernel/elf64.h>

struct symbol {
    const char *name;
    uintptr_t addr;
};

uintptr_t ksym_addr(const char *name);
const char *ksym_name(uintptr_t addr);
void ksym_expand(size_t count);
int  ksym_register(const char *name, uintptr_t addr);