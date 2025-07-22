#pragma once
#include <stdint.h>

uint64_t now(void);
void gettimeofday(long *sec, long *nsec);
uint64_t uptime(void);