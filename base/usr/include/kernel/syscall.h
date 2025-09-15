#pragma once
#include <stddef.h>

#ifdef __x86_64__
#include <kernel/arch/x86_64/syscall.h>
#elif __aarch64__
#include <kernel/arch/aarch64/syscall.h>
#endif

void syscall_handler(size_t *args);