#pragma once
#include <stdint.h>

void gettimeofday(long *sec, long *nsec);
uint64_t now(void);