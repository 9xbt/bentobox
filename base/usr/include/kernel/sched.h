#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/context.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>

#define SCHED_BITMAP_SIZE   4096

enum thread_state {
    THREAD_NEW,
    THREAD_RUNNING,
    THREAD_PAUSED,
    THREAD_ZOMBIE,
    THREAD_ZOMBIE_ACK,
};

enum process_state {
    PROCESS_ALIVE,
    PROCESS_ZOMBIE
};

struct thread {
    int tid;
    enum thread_state state;
    struct context ctx;
    struct process *parent;
    struct cpu *cpu;
};

struct process {
    char *name;
    uint64_t *pm;
    int pid;
    bool user;
    enum process_state state;
    struct vma *vma;
    struct file *files;
    int max_files;

    struct process *parent;
    list_t *children;

    list_t *threads;
};

#define this ((struct thread *)(this_core()->current_tcb ? this_core()->current_tcb->value : NULL))
#define this_proc ((struct process *)(this_core()->current_tcb ? ((struct thread *)this_core()->current_tcb->value)->parent : NULL))

node_t *sched_add_process(struct process *proc);
struct thread  *sched_new_thread(struct process *parent, void *entry);
struct process *sched_new_process(const char *name, bool user);
void sched_yield(void);
void sched_kill(struct process *proc);
void sched_schedule(struct registers *r);
void sched_install(void);