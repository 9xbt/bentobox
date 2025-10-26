#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/context.h>
#include <kernel/signal.h>
#include <kernel/list.h>
#include <kernel/file.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <kernel/vfs.h>

#define SCHED_BITMAP_SIZE   4096
#define SCHED_VMA_BASE      0x555555554000
#define SCHED_VMA_SIZE      256 * 1024 * 1024

enum thread_state {
    THREAD_RUNNING,
    THREAD_PAUSED,
    THREAD_ZOMBIE,
    THREAD_ZOMBIE_ACK,
    THREAD_SLEEPING
};

enum process_state {
    PROCESS_ALIVE,
    PROCESS_ZOMBIE,
    PROCESS_ZOMBIE_ALL
};

struct thread {
    int tid;
    enum thread_state state;
    struct context ctx;
    struct process *parent;
    struct cpu *cpu;
    struct registers *syscall_regs;
    sigset_t psig;
    bool doing_user_copy;
    long user_copy_status;
    size_t sleep_end;
};

struct process {
    char *name;
    uint64_t *pm;
    int pid;
    int pgid;
    bool user;
    enum process_state state;
    struct vma *vma;
    struct file *files;
    int max_files;
    struct vfs_node *cwd;
    sigset_t psig;

    struct process *parent;
    list_t *children;

    list_t *threads;
};

#define this ((struct thread *)(this_core()->current_tcb ? this_core()->current_tcb->value : NULL))
#define this_proc ((struct process *)(this_core()->current_tcb ? ((struct thread *)this_core()->current_tcb->value)->parent : NULL))

extern list_t *processes;
extern struct process *init_proc;
extern struct thread  *cleaner_tcb;

struct cpu *sched_find_cpu(void);
node_t *sched_add_process(struct process *proc);
struct process *sched_find_process(long pid);
struct process *sched_find_in_group(long pgid);
struct thread  *sched_new_thread(struct process *parent, void *entry, int argc, char *argv[], char *envp[]);
struct process *sched_new_process(const char *name, bool user);
long fork(void);
void sched_yield(void);
void sched_sleep(size_t ns);
void sched_exit(struct thread *tcb);
void sched_exit_group(struct process *proc);
void sched_schedule(struct registers *r);
void sched_install(void);