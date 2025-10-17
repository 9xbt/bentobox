#pragma once
#include <stddef.h>
#include <stdint.h>

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct timeval {
	int64_t tv_sec;
	int64_t tv_usec;
};

void arch_sleep(size_t ns);
void uptime(size_t *sec, size_t *nsec);
uint64_t now(void);