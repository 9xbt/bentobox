#pragma once
#include <stdint.h>

typedef volatile uint32_t spinlock_t;

void acquire(spinlock_t *lock);
void release(spinlock_t *lock);