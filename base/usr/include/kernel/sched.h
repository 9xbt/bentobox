#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/context.h>
#include <kernel/list.h>
#include <kernel/smp.h>

enum thread_state {
    THREAD_NEW,
    THREAD_RUNNING,
    THREAD_PAUSED,
};

struct thread {
    int tid;
    enum thread_state state;
    struct context ctx;
    struct process *parent;
};

struct process {
    char *name;
    uint64_t *pm;
    int pid;
    bool user;

    struct process *parent;
    list_t *children;

    list_t *threads;
};

#define this ((struct thread *)(this_core()->current_tcb ? this_core()->current_tcb->value : NULL))

node_t *sched_add_process(struct cpu *cpu, struct process *proc);
struct thread  *sched_new_thread(struct process *parent, void *entry);
struct process *sched_new_process(void *entry, const char *name, bool user);
void sched_schedule(struct registers *r);
void sched_install(void);