#pragma once
#include <stdatomic.h>
#include <kernel/sched.h>

typedef struct mutex_list {
    struct process *proc;
    struct mutex_list *next;
    struct mutex_list *last;
} mutex_list_t;

typedef struct mutex {
    int locked;
    struct process *owner;
    struct mutex_list *queue;
    atomic_flag lock;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);