#include <kernel/unixpipe.h>
#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/socket.h>
#include <kernel/elf64.h>
#include <kernel/panic.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/smp.h>
#include <stddef.h>

extern void arch_context_init(struct thread *tcb, void *entry, bool user, void *stack);
extern void arch_context_free(struct thread *tcb);
extern void arch_context_fork(struct thread *tcb);
extern void arch_save_context(void);
extern void arch_restore_context(void);
extern void arch_jumpstart(void);
extern void arch_yield(struct cpu *cpu);

list_t *processes = NULL;
list_t *zombie_threads = NULL;

uint8_t *pid_bitmap = NULL;
uint8_t *tid_bitmap = NULL;
spinlock_t pid_lock = 0;
spinlock_t tid_lock = 0;
spinlock_t balance_lock = 0;

struct process *init_proc = NULL;
struct thread  *cleaner_tcb = NULL;

int sched_allocate_pid(void) {
    acquire(&pid_lock);
    for (int pid = 0; pid < SCHED_BITMAP_SIZE * 8; pid++) {
        if (!bitmap_get(pid_bitmap, pid)) {
            bitmap_set(pid_bitmap, pid);
            release(&pid_lock);
            return pid;
        }
    }
    release(&pid_lock);
    return -1;
}

int sched_allocate_tid(void) {
    acquire(&tid_lock);
    for (int tid = 0; tid < SCHED_BITMAP_SIZE * 8; tid++) {
        if (!bitmap_get(tid_bitmap, tid)) {
            bitmap_set(tid_bitmap, tid);
            release(&tid_lock);
            return tid;
        }
    }
    release(&tid_lock);
    return -1;
}

void sched_free_pid(int pid) {
    acquire(&pid_lock);
    bitmap_clear(pid_bitmap, pid);
    release(&pid_lock);
}

void sched_free_tid(int tid) {
    acquire(&tid_lock);
    bitmap_clear(tid_bitmap, tid);
    release(&tid_lock);
}

int sched_get_idle_usage(struct cpu *cpu) {
    return cpu->total_time == 0 ? 100 : (cpu->idle_time * 100 / cpu->total_time);
}

int sched_get_busy_usage(struct cpu *cpu) {
    return 100 - sched_get_idle_usage(cpu);
}

int sched_get_user_processes(void) {
    int count = 0;
    foreach_safe(i, processes) {
        struct process *proc = i->value;
        if (proc->user && proc->state == PROCESS_ALIVE)
            count++;
    }
    return count;
}

struct cpu *sched_find_cpu(void) {
    if (cpu_count == 1)
        return this_cpu;

    struct cpu *target = this_cpu;
    int min = sched_get_busy_usage(target);

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        int usage = sched_get_busy_usage(core);
        if (usage < min) {
            min = usage;
            target = core;
        }
    }

    return target;
}

node_t *sched_add_process(struct process *proc) {
    foreach(thread, proc->threads) {
        struct cpu *cpu = sched_find_cpu();
        list_insert(cpu->threads, thread->value);
    }
    return list_insert(processes, proc);
}

struct process *sched_find_process(long pid) {
    foreach(i, processes) {
        struct process *proc = i->value;
        if (proc->pid == pid)
            return proc;
    }
    return NULL;
}

struct process *sched_find_in_group(long pgid) {
    foreach(i, processes) {
        struct process *proc = i->value;
        if (proc->pgid == pgid)
            return proc;
    }
    return NULL;
}

void sched_setup_stack(struct thread *tcb, int argc, char *argv[], char *envp[], Elf64_auxv_t *auxv, int auxc) {
    struct context *ctx = &tcb->ctx;

    int envc = 0;
    if (envp) for (; envp[envc]; envc++);

    uintptr_t *pm = mmu_get_pm();
    mmu_switch_pm(tcb->parent->pm);

    long depth = ((argc + envc + auxc) % 2 == 0) ? 16 : 24;

    uint64_t argv_ptrs[argc + 1];
    uint64_t env_ptrs[envc + 1];
    argv_ptrs[argc] = 0;
    env_ptrs[envc] = 0;

    int i = 0;
    for (i = 0; i < envc; i++) {
        depth += ALIGN_UP(strlen(envp[i]) + 1, 16);
        env_ptrs[i] = (uint64_t)(ctx->user_stack - depth);
        strcpy((char *)ctx->user_stack - depth, envp[i]);
    }
    for (i = 0; i < argc; i++) {
        depth += ALIGN_UP(strlen(argv[i]) + 1, 16);
        argv_ptrs[i] = (uint64_t)(ctx->user_stack - depth);
        strcpy((char *)ctx->user_stack - depth, argv[i]);
    }

    #define PUSH(x) (*(uint64_t *)(ctx->user_stack - (depth += 8)) = (x))

    if (auxv) {
        for (i = auxc - 1; i >= 0; i--) {
            PUSH(auxv[i].a_un.a_val);
            PUSH(auxv[i].a_type);
        }
    }

    PUSH(0);
    for (i = envc - 1; i >= 0; i--) {
        PUSH(env_ptrs[i]);
    }

    PUSH(0);
    for (i = argc - 1; i >= 0; i--) {
        PUSH(argv_ptrs[i]);
    }

    PUSH(argc);

    ctx->user_stack -= depth;

    #ifdef __x86_64__
    ctx->regs.rsp = ctx->user_stack;
    #endif

    mmu_switch_pm(pm);
}

struct thread *sched_new_thread(struct process *parent, void *entry, int argc, char *argv[], char *envp[], Elf64_auxv_t *auxv, int auxc, void *stack) {
    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = sched_allocate_tid();
    tcb->state = THREAD_READY;
    tcb->parent = parent;
    tcb->cpu = NULL;
    tcb->syscall_regs = NULL;
    tcb->doing_user_copy = false;
    tcb->user_copy_status = 0;
    tcb->sleep_end = 0;
    tcb->wakeup_pending = false;
    tcb->sigframe = NULL;
    tcb->start_time = 0;
    tcb->end_time = 0;
    tcb->last_cpu_time = 0;
    tcb->lock = 0;

    arch_context_init(tcb, entry, parent->user, stack);

    if (parent->user && !stack) {
        static char *empty_argv_envp[] = { NULL };
        sched_setup_stack(tcb, argc, argv ? argv : empty_argv_envp, envp ? envp : empty_argv_envp, auxv, auxc);
    }
    
    list_insert(parent->threads, tcb);
    return tcb;
}

struct process *sched_new_process(const char *name, bool user) {
    struct process *proc = kmalloc(sizeof(struct process));
    proc->name = strdup(name);
    proc->pm = mmu_create_pagemap();
    proc->pid = sched_allocate_pid();
    proc->pgid = 0;
    proc->sid = 0;
    proc->user = user;
    proc->state = PROCESS_ALIVE;
    proc->parent = NULL;
    proc->children = list_create();
    proc->threads = list_create();
    proc->dead_children = list_create();
    proc->vma = vma_create(SCHED_VMA_BASE, SCHED_VMA_SIZE);
    proc->max_files = 16;
    proc->files = kmalloc(sizeof(struct file) * proc->max_files);
    memset(proc->files, 0, sizeof(struct file) * proc->max_files);
    vfs_result_t r = vfs_open(NULL, "/dev/tty1", 0);
    assert(r.node);
    proc->files[0] = proc->files[1] = proc->files[2] = file_new(r.node, 0);
    proc->cwd = vfs_get_root();
    proc->umask = 022;
    proc->exit_status = 0;
    memset(&proc->psig, 0, sizeof proc->psig);
    memset(&proc->sighand, 0, sizeof proc->sighand);
    memset(&proc->blocked, 0, sizeof proc->blocked);

    if (proc->pid == 1)
        init_proc = proc;

    dprintf(LOG_DEBUG, "\033[93msched:\033[0m created process '%s' with pid %d\n", name, proc->pid);
    return proc;
}

long fork(void) {
    struct process *proc = kmalloc(sizeof(struct process));
    proc->name = strdup(this_proc->name);
    proc->pm = mmu_create_pagemap();
    proc->pid = sched_allocate_pid();
    proc->pgid = this_proc->pgid;
    proc->sid = this_proc->sid;
    proc->user = true;
    proc->state = PROCESS_ALIVE;
    proc->parent = this_proc;
    proc->children = list_create();
    proc->threads = list_create();
    proc->dead_children = list_create();
    proc->vma = vma_clone(this_proc->vma, proc->pm);

    proc->max_files = this_proc->max_files;
    proc->files = kmalloc(sizeof(struct file) * proc->max_files);
    memcpy(proc->files, this_proc->files, sizeof(struct file) * proc->max_files);
    for (int i = 0; i < proc->max_files; i++) {
        struct file *file = &proc->files[i];
        if (!file || !file->open || !file->node)
            continue;
        file->node->refcount++;
        if (file->node->type == VFS_UNIXPIPE) {
            struct unix_pipe *pipe = file->node->device;
            if (!strcmp(file->node->name, "[pipe::read]"))
                pipe->read_refs++;
            else if (!strcmp(file->node->name, "[pipe::write]"))
                pipe->write_refs++;
        }
    }
    proc->cwd = this_proc->cwd;
    proc->cwd->refcount++;
    proc->umask = this_proc->umask;
    memset(&proc->psig, 0, sizeof proc->psig);
    memcpy(&proc->sighand, &this_proc->sighand, sizeof proc->sighand);
    memcpy(&proc->blocked, &this_proc->blocked, sizeof proc->blocked);

    list_insert(this_proc->children, proc);

    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = sched_allocate_tid();
    tcb->state = THREAD_READY;
    tcb->parent = proc;
    tcb->cpu = NULL;
    tcb->syscall_regs = NULL;
    tcb->doing_user_copy = false;
    tcb->user_copy_status = 0;
    tcb->sleep_end = 0;
    tcb->wakeup_pending = false;
    tcb->sigframe = NULL;
    tcb->start_time = 0;
    tcb->end_time = 0;
    tcb->last_cpu_time = 0;
    tcb->lock = 0;
    arch_context_fork(tcb);
    
    list_insert(proc->threads, tcb);

    // dprintf(LOG_DEBUG, "\033[93msched:\033[0m forked process '%s' with pid %d\n", proc->name, proc->pid);
    sched_add_process(proc);
    return proc->pid;
}

void sched_yield(void) {
    arch_yield(this_cpu);
}

void sched_sleep(size_t ns) {
    acquire(&this->lock);
    if (this->wakeup_pending)
        this->wakeup_pending = false;
    size_t sec, nsec;
    uptime(&sec, &nsec);
    this->sleep_end = sec * 1000000000UL + nsec + ns;
    this->state = THREAD_SLEEPING;
    sched_yield();
}

void sched_block(struct thread *tcb, size_t ns) {
    acquire(&tcb->lock);
    if (tcb->wakeup_pending) {
        tcb->wakeup_pending = false;
        release(&tcb->lock);
        return;
    }

    if (ns > 0) {
        size_t sec, nsec;
        uptime(&sec, &nsec);
        tcb->sleep_end = sec * 1000000000UL + nsec + ns;
        tcb->state = THREAD_SLEEPING;
    } else {
        tcb->state = THREAD_PAUSED;
    }

    if (tcb == this)
        sched_yield();
    else
        release(&tcb->lock);
}

void sched_wake(struct thread *tcb) {
    acquire(&tcb->lock);
    tcb->wakeup_pending = true;
    if (tcb->state != THREAD_RUNNING)
        tcb->state = THREAD_READY;
    release(&tcb->lock);
}

void sched_exit(struct thread *tcb) {
    acquire(&tcb->lock);
    if (tcb->wakeup_pending)
        tcb->wakeup_pending = false;
    tcb->state = THREAD_ZOMBIE;

    struct process *proc = tcb->parent;
    for (int i = 0; i < proc->max_files; i++) {
        struct file *file = &proc->files[i];
        if (file->open) {
            acquire(&file->node->waiters_lock);
            foreach_safe(j, file->node->waiters) {
                if (j->value == this)
                    list_remove(file->node->waiters, j);
            }
            release(&file->node->waiters_lock);
        }
    }

    if (tcb == this) {
        sched_yield();
        for (;;) {}
    }
    release(&tcb->lock);
}

void sched_exit_group(struct process *proc, int status) {
    proc->state = PROCESS_ZOMBIE;
    proc->exit_status = status;
    
    if (proc == this_proc) {
        sched_exit(this);
        __builtin_unreachable();
    }
}

void sched_balance(void) {
    if (!try_acquire(&balance_lock))
        return;
    
    if (cpu_count == 1) {
        release(&balance_lock);
        return;
    }

    struct cpu *max_cpu = NULL, *min_cpu = NULL;
    int max_load = 0, min_load = 100;
    
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        int load = sched_get_busy_usage(core);
        
        if (load > max_load) {
            max_load = load;
            max_cpu = core;
        }
        if (load < min_load) {
            min_load = load;
            min_cpu = core;
        }
    }
    
    if (max_load - min_load >= SCHED_IMBALANCE_THRESHOLD && max_cpu && min_cpu && max_cpu->threads->length > 1) {
        foreach_safe(node, max_cpu->threads) {
            struct thread *tcb = node->value;
            if (try_acquire(&tcb->lock) && tcb->state == THREAD_READY && max_cpu->current_tcb != node && tcb != max_cpu->idle_tcb->value) {
                list_remove(max_cpu->threads, node);
                list_insert(min_cpu->threads, tcb);
                release(&tcb->lock);
                break;
            }
        }
    }
    
    release(&balance_lock);
}

node_t *sched_find_next(void) {
    size_t sec, nsec;
    uptime(&sec, &nsec);
    size_t now = sec * 1000000000UL + nsec;

    node_t *start = (this_cpu->current_tcb && this_cpu->current_tcb->next) ? this_cpu->current_tcb->next : this_cpu->threads->head, *node = start;
    do {
        struct thread *t = (struct thread *)node->value;
        node_t *next = node->next ? node->next : this_cpu->threads->head;

        try_acquire(&t->lock);
        if (t->state == THREAD_ZOMBIE) {
            list_remove(this_cpu->threads, node);
            list_insert(zombie_threads, t);
            release(&t->lock);
            if (cleaner_tcb->state == THREAD_PAUSED)
                cleaner_tcb->state = THREAD_READY;
            node = next;
            continue;
        }
        if (t->state == THREAD_SLEEPING && now >= t->sleep_end)
            t->state = THREAD_READY;
        if (t->state == THREAD_READY)
            return node;
        release(&t->lock);

        node = next;
    } while (node != start);

    return this_cpu->idle_tcb;
}

void sched_schedule(struct registers *r) {
    size_t sec, nsec;
    uptime(&sec, &nsec);
    uint64_t now = sec * 1000000000UL + nsec;

    if (now - this_cpu->last_reset >= 200000000UL) {
        this_cpu->idle_time = 0;
        this_cpu->total_time = 0;
        this_cpu->last_reset = now;
        
        // sched_balance();
    }

    if (this_cpu->current_tcb) {
        memcpy(&(this->ctx.regs), r, sizeof(struct registers));
        arch_save_context();

        if (this->state == THREAD_RUNNING)
            this->state = THREAD_READY;
        this->end_time = now;
        this->last_cpu_time = this->end_time - this->start_time;

        if (this_proc->state == PROCESS_ZOMBIE)
            this->state = THREAD_ZOMBIE;
        if (this->state == THREAD_ZOMBIE) {
            struct thread *tcb = this;
            list_remove_value(this_cpu->threads, tcb);
            list_insert(zombie_threads, tcb);
            this_cpu->current_tcb = this_cpu->threads->head;
            if (cleaner_tcb->state == THREAD_PAUSED)
                cleaner_tcb->state = THREAD_READY;
        }
        
        if (this_cpu->current_tcb == this_cpu->idle_tcb)
            this_cpu->idle_time += this->last_cpu_time;
        this_cpu->total_time += this->last_cpu_time;

        release(&this->lock);
        this_cpu->current_tcb = sched_find_next();
    } else {
    find_next:
        this_cpu->current_tcb = sched_find_next();
    }

    enum thread_state state = this->state;
    this->cpu = this_cpu;
    signal_check_pending(this);
    if (this->state != state) {
        release(&this->lock);
        goto find_next;
    }

    this->start_time = now;
    if (this->state == THREAD_READY)
        this->state = THREAD_RUNNING;
    release(&this->lock);

    memcpy(r, &(this->ctx.regs), sizeof(struct registers));
    arch_restore_context();
}

void idle(void) {
    for (;;) {
        wfi();
    }
}

void sched_cleaner(void) {
    for (;;) {
        // sched_sleep(10000000);

        foreach_safe(i, zombie_threads) {
            struct thread *tcb = i->value;
            struct process *proc = tcb->parent;
            list_remove_value(proc->threads, tcb);

            if (proc->threads->length == 0) {
                if (init_proc == proc)
                    panic("Tried to kill init!");

                if (proc->parent) {
                    struct dead_process *dp = kmalloc(sizeof(struct dead_process));
                    dp->pid = proc->pid;
                    dp->status = proc->exit_status;
                    list_insert(proc->parent->dead_children, dp);
                    signal_send(proc->parent, SIGCHLD);
                    list_remove_value(proc->parent->children, proc);
                }

                foreach_safe(j, proc->children) {
                    struct process *child = j->value;
                    child->parent = init_proc;
                }
                list_free(proc->children);

                foreach_safe(j, proc->dead_children) {
                    struct dead_process *dp = j->value;
                    if (dp)
                        kfree(dp);
                }
                list_free(proc->dead_children);

                for (int j = 0; j < proc->max_files; j++) {
                    struct file *file = &proc->files[j];
                    if (file->open) {
                        if (file->node->type == VFS_SOCKET)
                            socket_shutdown_node(file->node);
                        vfs_close(file->node);
                    }
                }

                vfs_close(proc->cwd);
                vma_destroy(proc->vma, proc->pm);
                mmu_destroy_pagemap(proc->pm);
                kfree(proc->files);
                kfree(proc->name);
                sched_free_pid(proc->pid);

                list_remove_value(processes, proc);
                list_free(proc->threads);
                kfree(proc);
            }

            arch_context_free(tcb);
            sched_free_tid(tcb->tid);
            kfree(tcb);
            list_remove(zombie_threads, i);
        }

        if (!zombie_threads->length) {
            this->state = THREAD_PAUSED;
            sched_yield();
        }
    }
}

void sched_install(void) {
    processes      = list_create();
    zombie_threads = list_create();
    pid_bitmap = kmalloc(SCHED_BITMAP_SIZE);
    tid_bitmap = kmalloc(SCHED_BITMAP_SIZE);
    memset(pid_bitmap, 0, SCHED_BITMAP_SIZE);
    memset(tid_bitmap, 0, SCHED_BITMAP_SIZE);
    bitmap_set(pid_bitmap, 1);

    struct process *idle_proc = sched_new_process("idle angel", false);
    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        struct thread *tcb = sched_new_thread(idle_proc, idle, 0, NULL, NULL, NULL, 0, NULL);
        tcb->state = THREAD_PAUSED;
        core->idle_tcb = list_insert(core->threads, tcb);
    }

    struct process *cleaner = sched_new_process("psycho killer", false);
    cleaner_tcb = sched_new_thread(cleaner, sched_cleaner, 0, NULL, NULL, NULL, 0, NULL);
    cleaner_tcb->state = THREAD_PAUSED;
    sched_add_process(cleaner);

    dprintf(LOG_INFO, "\033[93msched:\033[0m initialized scheduler\n");
}

void sched_shutdown(void) {
    foreach_safe(i, processes) {
        struct process *proc = i->value;
        if (proc != this_proc && proc->user)
            sched_exit_group(proc, SIGKILL);
    }

    while (sched_get_user_processes() > 1) {
        sched_yield();
    }

    dprintf(LOG_DEBUG, "\033[93msched:\033[0m Goodbye!\n");
}