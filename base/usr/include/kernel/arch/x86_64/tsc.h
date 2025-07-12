#pragma once
#include <stddef.h>
#include <stdint.h>

extern uint64_t tsc_period;

void tsc_install();
void tsc_sleep(size_t us);
void tsc_read_time(long *sec, long *nsec);
uint64_t tsc_get_ticks(void);