#pragma once
#include <stddef.h>

void arch_sleep(size_t ns);
void uptime(long *sec, long *nsec);