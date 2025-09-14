#pragma once

#ifdef __x86_64__
#include <kernel/arch/x86_64/syscall.h>
#elif __aarch64__
#endif

typedef long (*syscall_func)(long, long, long, long, long, long);

extern syscall_func syscalls[SYSCALL_COUNT];