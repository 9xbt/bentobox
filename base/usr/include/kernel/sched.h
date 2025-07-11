#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/mmu/vma.h>
#include <kernel/elf64.h>
#include <kernel/malloc.h>

#define USER_STACK_SIZE 256
#define USER_STACK_TOP  0x00007ffffffff000
#define USER_MAX_CHILDS 16
#define USER_MAX_FDS    16

enum process_state {
    TASK_RUNNING,
    TASK_PAUSED,
    TASK_SLEEPING,
    TASK_KILLED,
    TASK_FRESH,
    TASK_SIGNAL,
    TASK_BLOCKING_IO
};

struct process_time {
    uint64_t start;
    uint64_t end;
    uint64_t last;
};

struct process_section {
    uintptr_t ptr;
    size_t length;
};

struct process {
    uint64_t stack;
    uint64_t kernel_stack;
    uint64_t gs;
    uint64_t fs;
    struct registers ctx;
    char fxsave[512];
    uint64_t user_gs;

    char *name;
    uint64_t *pml4;
    long pid;
    bool user;
    enum process_state state;
    struct process_time time;
    struct fd fd_table[USER_MAX_FDS];
    struct process_section sections[16];
    uint64_t stack_bottom;
    uint64_t stack_bottom_phys;
    uint64_t kernel_stack_bottom;
    struct vma_head *vma;
    struct vfs_node *cwd;
    uintptr_t brk;

    uint32_t pending_signals;
    uint32_t signal_mask;
    int signal_data;
    void (*signal_handlers[32])(struct process *);
    
    struct process *parent;
    list_t *children;
    list_t *poll_list;
};

#define this ((struct process *)(this_core()->current_proc ? this_core()->current_proc->value : NULL))
#define process_list this_core()->processes
#define terminated_process_list this_core()->terminated_processes

void sched_install(void);
void sched_jumpstart(void);
void sched_yield(void);
void sched_lock(void);
void sched_unlock(void);
void sched_block(enum process_state reason);
void sched_unblock(struct process *proc);
void sched_sleep(int us);
void sched_kill(struct process *proc, int status);
void sched_idle(void);
node_t *sched_add_task(struct process *proc, struct cpu *core);
struct process *sched_new_task(void *entry, const char *name);
struct process *sched_new_user_task(void *entry, const char *name, int argc, char *argv[], char *env[]);
struct process *sched_get_foreground(void);
struct process *sched_find_process(long pid);