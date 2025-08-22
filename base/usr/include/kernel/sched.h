#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/elf64.h>
#include <kernel/malloc.h>

#define CSIGNAL		        0x000000ff	/* signal mask to be sent at exit */
#define CLONE_VM	        0x00000100	/* set if VM shared between processes */
#define CLONE_FS	        0x00000200	/* set if fs info shared between processes */
#define CLONE_FILES	        0x00000400	/* set if open files shared between processes */
#define CLONE_SIGHAND	    0x00000800	/* set if signal handlers and blocked signals shared */
#define CLONE_PIDFD	        0x00001000	/* set if a pidfd should be placed in parent */
#define CLONE_PTRACE	    0x00002000	/* set if we want to let tracing continue on the child too */
#define CLONE_VFORK	        0x00004000	/* set if the parent wants the child to wake it up on mm_release */
#define CLONE_PARENT	    0x00008000	/* set if we want to have the same parent as the cloner */
#define CLONE_THREAD	    0x00010000	/* Same thread group? */
#define CLONE_NEWNS	        0x00020000	/* New mount namespace group */
#define CLONE_SYSVSEM	    0x00040000	/* share system V SEM_UNDO semantics */
#define CLONE_SETTLS	    0x00080000	/* create a new TLS for the child */
#define CLONE_PARENT_SETTID	0x00100000	/* set the TID in the parent */
#define CLONE_CHILD_CLEARTI	0x00200000	/* clear the TID in the child */
#define CLONE_DETACHED		0x00400000	/* Unused, ignored */
#define CLONE_UNTRACED		0x00800000	/* set if the tracing process can't force CLONE_PTRACE on this clone */
#define CLONE_CHILD_SETTID	0x01000000	/* set the TID in the child */
#define CLONE_NEWCGROUP		0x02000000	/* New cgroup namespace */
#define CLONE_NEWUTS		0x04000000	/* New utsname namespace */
#define CLONE_NEWIPC		0x08000000	/* New ipc namespace */
#define CLONE_NEWUSER		0x10000000	/* New user namespace */
#define CLONE_NEWPID		0x20000000	/* New pid namespace */
#define CLONE_NEWNET		0x40000000	/* New network namespace */
#define CLONE_IO		    0x80000000	/* Clone io context */

#define USER_STACK_SIZE 256
#define USER_STACK_TOP  0x00007ffffffff000
#define USER_MAX_CHILDS 16
#define USER_MAX_FDS    32

enum process_state {
    TASK_RUNNING,
    TASK_PAUSED,
    TASK_SLEEPING,
    TASK_KILLED,
    TASK_FRESH,
    TASK_SIGNAL,
    TASK_POLLING
};

struct process_time {
    uint64_t start;
    uint64_t end;
    uint64_t last;
    uint64_t total;
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
    long pgid;
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

    struct registers *syscall_ctx;
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
void sched_sleep(long us);
void sched_kill(struct process *proc, int status);
void sched_idle(void);
node_t *sched_add_task(struct process *proc, struct cpu *core);
struct process *sched_new_task(void *entry, const char *name);
struct process *sched_new_user_task(void *entry, const char *name, int argc, char *argv[], char *env[]);
struct process *sched_get_foreground(long pgid);
struct process *sched_find_process(long pid);