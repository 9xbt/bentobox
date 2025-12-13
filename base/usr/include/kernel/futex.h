#pragma once
#include <kernel/sched.h>

struct futex_waiter {
    int *address;
    struct thread *thread;
};

void futex_initialize(void);
long futex_wait(int *pointer, int expected, const struct timespec *time);
long futex_wake(int *pointer);