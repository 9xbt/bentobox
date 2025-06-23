#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/vma.h>
#include <kernel/elf64.h>
#include <kernel/malloc.h>

#define USER_STACK_SIZE 256
#define USER_STACK_TOP  0x00007ffffffff000
#define USER_MAX_CHILDS 16
#define USER_MAX_FDS    16

enum task_state {
    TASK_RUNNING,
    TASK_PAUSED,
    TASK_SLEEPING,
    TASK_KILLED,
    TASK_FRESH,
    TASK_SIGNAL,
    TASK_BLOCKING_IO
};

struct task_time {
    uint64_t start;
    uint64_t end;
    uint64_t last;
};

struct task_section {
    uintptr_t ptr;
    size_t length;
};

struct task {
    uint64_t stack;
    uint64_t kernel_stack;
    uint64_t gs;
    uint64_t fs;
    struct registers ctx;
    char align[8];
    char fxsave[512];

    struct task *next;
    struct task *prev;
    char *name;
    uint64_t *pml4;
    long pid;
    bool user;
    enum task_state state;
    struct task_time time;
    struct fd fd_table[USER_MAX_FDS];
    struct task_section sections[16];
    uint64_t stack_bottom;
    uint64_t stack_bottom_phys;
    uint64_t kernel_stack_bottom;
    struct vma_head *vma;
    uint64_t user_gs;
    struct vfs_node *cwd;

    uint32_t pending_signals;
    void (*signal_handlers[16])(struct task *, int);
    struct task *parent;
    struct task *children;
    int child_exit;
};

#define this this_core()->current_proc
#define process_list this_core()->processes

void sched_install(void);
void sched_start_all_cores(void);
void sched_yield(void);
void sched_lock(void);
void sched_unlock(void);
void sched_block(enum task_state reason);
void sched_unblock(struct task *proc);
void sched_sleep(int us);
void sched_kill(struct task *proc, int status);
void sched_idle(void);
void sched_add_task(struct task *proc, struct cpu *core);
struct task *sched_new_task(void *entry, const char *name);
struct task *sched_new_user_task(void *entry, const char *name, int argc, char *argv[], char *env[]);