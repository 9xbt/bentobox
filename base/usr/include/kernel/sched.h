#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/spinlock.h>
#include <kernel/context.h>
#include <kernel/signal.h>
#include <kernel/elf64.h>
#include <kernel/list.h>
#include <kernel/file.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <kernel/vfs.h>

#define SCHED_BITMAP_SIZE   4096
#define SCHED_VMA_BASE      0x555555554000
#define SCHED_VMA_SIZE      256 * 1024 * 1024
#define SCHED_USER_STACK_PAGES  256
#define SCHED_USER_STACK_SIZE   256 * PAGE_SIZE
#define SCHED_KERNEL_STACK_SIZE 16 * PAGE_SIZE
#define SCHED_IMBALANCE_THRESHOLD 20
#define SCHED_KILLABLE(tcb) (tcb->syscall_regs ? (tcb->state != THREAD_RUNNING && tcb->state != THREAD_ZOMBIE) : true)

#ifdef __x86_64__
#define wfi() asm ("hlt");
#define cli() asm ("cli");
#define sti() arch_sti();
#elif __aarch64__
#define wfi() asm ("wfi");
#endif

enum thread_state {
    THREAD_RUNNING,
    THREAD_READY,
    THREAD_PAUSED,
    THREAD_ZOMBIE,
    THREAD_SLEEPING
};

enum process_state {
    PROCESS_ALIVE,
    PROCESS_ZOMBIE,
};

struct dead_process {
    int pid;
    int status;
};

struct thread {
    int tid;
    enum thread_state state;
    struct context ctx;
    struct process *parent;
    struct cpu *cpu;
    struct thread *self;
    struct registers *syscall_regs;
    bool doing_user_copy;
    long user_copy_status;
    size_t sleep_end;
    bool wakeup_pending;
    bool kill_pending;
    struct sigframe *sigframe;
    uint64_t start_time;
    uint64_t end_time;
    uint64_t last_cpu_time;
    spinlock_t lock;
    int refcount;
};

struct process {
    char *name;
    uint64_t *pm;
    int pid;
    int pgid;
    int sid;
    bool user;
    enum process_state state;
    struct vma *vma;
    struct file *files;
    int max_files;
    struct vfs_node *cwd;
    unsigned int umask;
    sigset_t psig;

    struct process *parent;
    list_t *children;
    list_t *threads;
    list_t *dead_children;
    int exit_status;

    struct sigaction sighand[_NSIG];
    sigset_t blocked;
};

extern list_t *processes;
extern struct process *init_proc;
extern struct thread  *cleaner_tcb;

extern void arch_sti();

struct cpu *sched_find_cpu(void);
node_t *sched_add_process(struct process *proc);
struct process *sched_find_process(long pid);
struct process *sched_find_in_group(long pgid);
struct thread  *sched_new_thread(struct process *parent, void *entry, int argc, char *argv[], char *envp[], Elf64_auxv_t *auxv, int auxc, void *stack);
struct process *sched_new_process(const char *name, bool user);
long fork(void);
int  sched_allocate_pid(void);
int  sched_allocate_tid(void);
void sched_free_pid(int pid);
void sched_free_tid(int tid);
bool sched_pid_exists(int pid);
void sched_yield(void);
void sched_sleep(size_t ns);
void sched_block(struct thread *tcb, size_t ns);
void sched_wake(struct thread *tcb);
bool sched_exit(struct thread *tcb);
void sched_exit_group(struct process *proc, int status);
void sched_clean_tcb(struct thread *tcb);
void sched_schedule(struct registers *r);
void sched_install(void);