#pragma once
#include <stdint.h>

uint64_t now(void);
void gettimeofday(long *sec, long *nsec);
void uptime(long *sec, long *nsec);