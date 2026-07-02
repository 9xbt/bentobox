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
#include <kernel/errno.h>
#include <kernel/file.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/smp.h>
#include <stddef.h>

extern void arch_context_init(struct thread *tcb, void *entry, bool user, void *stack);
extern void arch_context_free(struct thread *tcb);
extern void arch_context_fork(struct thread *tcb);
extern void arch_save_context(struct registers *r);
extern void arch_restore_context(struct registers *r);
extern void arch_jumpstart(void);
extern void arch_yield(struct cpu *cpu);

list_t *processes = NULL;
list_t *zombie_threads = NULL;
int user_procs = 0;

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

struct cpu *sched_find_cpu(void) {
    if (cpu_count == 1)
        return get_core(0);

    struct cpu *target = get_core_logical(get_logical_id());
    cli();
    acquire(&target->threads->lock);
    size_t target_threads = target->threads->length;
    release(&target->threads->lock);

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *cpu = get_core(i);
        acquire(&cpu->threads->lock);
        if (cpu->threads->length < target_threads) {
            target = cpu;
            target_threads = cpu->threads->length;
        }
        release(&cpu->threads->lock);
    }
    sti();

    return target;
}

node_t *sched_add_process(struct process *proc) {
    acquire(&proc->threads->lock);
    foreach(thread, proc->threads) {
        struct cpu *cpu = sched_find_cpu();
        struct thread *tcb = thread->value;
        tcb->cpu = cpu;

        node_t *node = list_create_node(tcb);
        cli();
        acquire(&cpu->threads->lock);
        list_append(cpu->threads, node);
        release(&cpu->threads->lock);
        sti();
    }
    release(&proc->threads->lock);

    acquire(&processes->lock);
    node_t *node = list_insert(processes, proc);
    if (proc->user)
        __atomic_add_fetch(&user_procs, 1, __ATOMIC_RELAXED);
    release(&processes->lock);
    return node;
}

struct process *sched_find_process(long pid) {
    acquire(&processes->lock);
    foreach(i, processes) {
        struct process *proc = i->value;
        if (proc->pid == pid) {
            release(&processes->lock);
            return proc;
        }
    }
    release(&processes->lock);
    return NULL;
}

struct process *sched_find_in_group(long pgid) {
    acquire(&processes->lock);
    foreach(i, processes) {
        struct process *proc = i->value;
        if (proc->pgid == pgid) {
            release(&processes->lock);
            return proc;
        }
    }
    release(&processes->lock);
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
    tcb->self = tcb;
    tcb->syscall_regs = NULL;
    tcb->doing_user_copy = false;
    tcb->user_copy_status = 0;
    tcb->sleep_end = 0;
    tcb->wakeup_pending = false;
    tcb->kill_pending = false;
    tcb->signaled = false;
    tcb->sigframe = NULL;
    tcb->start_time = 0;
    tcb->end_time = 0;
    tcb->last_cpu_time = 0;
    tcb->lock = 0;
    tcb->refcount = 1;
    arch_context_init(tcb, entry, parent->user, stack);

    if (parent->user && !stack) {
        static char *empty_argv_envp[] = { NULL };
        sched_setup_stack(tcb, argc, argv ? argv : empty_argv_envp, envp ? envp : empty_argv_envp, auxv, auxc);
    }
    
    acquire(&parent->threads->lock);
    list_insert(parent->threads, tcb);
    release(&parent->threads->lock);
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
    for (int i = 0; i < 3; i++)
        proc->files[i] = file_new(vfs_open(NULL, "/dev/tty1", 0).node, 0);
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
        if (!file || !file->open)
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

    acquire(&this_proc->children->lock);
    list_insert(this_proc->children, proc);
    release(&this_proc->children->lock);

    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = sched_allocate_tid();
    tcb->state = THREAD_READY;
    tcb->parent = proc;
    tcb->cpu = NULL;
    tcb->self = tcb;
    tcb->syscall_regs = NULL;
    tcb->doing_user_copy = false;
    tcb->user_copy_status = 0;
    tcb->sleep_end = 0;
    tcb->wakeup_pending = false;
    tcb->kill_pending = false;
    tcb->signaled = false;
    tcb->sigframe = NULL;
    tcb->start_time = 0;
    tcb->end_time = 0;
    tcb->last_cpu_time = 0;
    tcb->lock = 0;
    tcb->refcount = 1;
    arch_context_fork(tcb);
    
    acquire(&proc->threads->lock);
    list_insert(proc->threads, tcb);
    release(&proc->threads->lock);

    // dprintf(LOG_DEBUG, "\033[93msched:\033[0m forked process '%s' with pid %d\n", proc->name, proc->pid);
    sched_add_process(proc);
    return proc->pid;
}

void sched_yield(void) {
    if (this->state == THREAD_RUNNING)
        this->state = THREAD_READY;
    arch_yield(this_cpu);
}

long sched_sleep(size_t ns) {
    cli();
    acquire(&this->lock);
    if (this->wakeup_pending)
        this->wakeup_pending = false;
    
    size_t sec, nsec;
    uptime(&sec, &nsec);
    this->sleep_end = sec * 1000000000UL + nsec + ns;
    this->state = THREAD_SLEEPING;

    sched_yield();
    sti();

    long ret = this->signaled ? -EINTR : 0;
    this->signaled = false;
    return ret;
}

long sched_block(struct thread *tcb, size_t ns) {
    cli();
    acquire(&tcb->lock);
    if (tcb->wakeup_pending || tcb->signaled) {
        tcb->wakeup_pending = false;
        long ret = tcb->signaled ? -EINTR : 0;
        tcb->signaled = false;

        release(&tcb->lock);
        sti();
        return ret;
    }

    if (ns > 0) {
        size_t sec, nsec;
        uptime(&sec, &nsec);
        tcb->sleep_end = sec * 1000000000UL + nsec + ns;
    } else {
        tcb->sleep_end = 0;
    }
    tcb->state = THREAD_PAUSED;

    if (tcb != this) {
        release(&tcb->lock);
        sti();
        return 0;
    }
    
    sched_yield();
    sti();

    long ret = tcb->signaled ? -EINTR : 0;
    tcb->signaled = false;
    return ret;
}

void sched_wake(struct thread *tcb) {
    cli();
    acquire(&tcb->lock);
    if (tcb->state == THREAD_ZOMBIE) {
        release(&tcb->lock);
        sti();
        return;
    }
    
    tcb->wakeup_pending = true;
    if (tcb->state != THREAD_RUNNING && tcb->state != THREAD_ZOMBIE)
        tcb->state = THREAD_READY;

    struct cpu *cpu = tcb->cpu;
    release(&tcb->lock);

    if (cpu && cpu != this_cpu && cpu->current_tcb == cpu->idle_tcb)
        arch_yield(cpu);
    sti();
}

bool sched_exit(struct thread *tcb) {
    if (!tcb->kill_pending) {
        struct process *proc = tcb->parent;
        for (int i = 0; i < proc->max_files; i++) {
            struct file *file = &proc->files[i];
            if (!file || !file->open)
                continue;

            vfs_node_t *node = file->node;
            acquire(&node->waiters->lock);
            list_remove_value(node->waiters, tcb);
            release(&node->waiters->lock);
        }
    }

    cli();
    acquire(&tcb->lock);
    if (tcb->state == THREAD_ZOMBIE) {
        release(&tcb->lock);
        sti();
        return true;
    }
    
    if (tcb->wakeup_pending)
        tcb->wakeup_pending = false;
    if (tcb != this && !SCHED_KILLABLE(tcb)) {
        tcb->kill_pending = true;
        release(&tcb->lock);
        sti();
        return false;
    }
    tcb->state = THREAD_ZOMBIE;

    release(&tcb->lock);
    if (tcb == this) {
        sched_yield();
        __builtin_unreachable();
    }
    sti();
    return true;
}

void sched_exit_group(struct process *proc, int status) {
    for (int fd = 0; fd < proc->max_files; fd++) {
        struct file *file = &proc->files[fd];
        if (!file->open)
            continue;
        file->open = false;

        if (file->node->type == VFS_SOCKET)
            socket_shutdown_node(file->node);
        vfs_close(file->node);
    }

    proc->state = PROCESS_ZOMBIE;
    proc->exit_status = status;

    if (proc == this_proc) {
        assert(sched_exit(this));
        __builtin_unreachable();
    }
}

node_t *sched_find_next(struct cpu *cpu, size_t now) {
    acquire(&cpu->threads->lock);
    
    node_t *start = (cpu->current_tcb && cpu->current_tcb->next) ? cpu->current_tcb->next : cpu->threads->head, *node = start;
    do {
        struct thread *t = (struct thread *)node->value;
        node_t *next = node->next ? node->next : cpu->threads->head;

        acquire(&t->lock);
        if ((t->parent->state == PROCESS_ZOMBIE || t->kill_pending) && SCHED_KILLABLE(t)) {
            t->state = THREAD_ZOMBIE;
            t->kill_pending = false;
        }
        if (t->state == THREAD_ZOMBIE) {
            acquire(&zombie_threads->lock);
            list_unlink(cpu->threads, node);
            list_append(zombie_threads, node);
            release(&zombie_threads->lock);
            
            release(&t->lock);
            if (cleaner_tcb->state == THREAD_PAUSED)
                cleaner_tcb->state = THREAD_READY;
            node = next;
            continue;
        }
        if ((t->state == THREAD_SLEEPING || t->state == THREAD_PAUSED) && t->sleep_end && now >= t->sleep_end) {
            t->state = THREAD_READY;
        }
        if (t->state == THREAD_READY || t->state == THREAD_RUNNING) {
            release(&t->lock);
            release(&cpu->threads->lock);
            return node;
        }
        release(&t->lock);

        node = next;
    } while (node && node != start);

    release(&cpu->threads->lock);
    return cpu->idle_tcb;
}

void sched_schedule(struct irq *irq, struct registers *r) {
    (void)irq;
    size_t sec, nsec;
    uptime(&sec, &nsec);
    size_t now = sec * 1000000000UL + nsec;

    if (get_core_logical(get_logical_id())->current_tcb) {
        arch_save_context(r);

        this->end_time = now;
        this->last_cpu_time = this->end_time - this->start_time;

        if ((this_proc->state == PROCESS_ZOMBIE || this->kill_pending) && SCHED_KILLABLE(this)) {
            this->state = THREAD_ZOMBIE;
            this->kill_pending = false;
        }
        if (this->state == THREAD_ZOMBIE) {
            struct node *tcb = this_cpu->current_tcb;
            acquire(&this_cpu->threads->lock);

            acquire(&zombie_threads->lock);
            list_unlink(this_cpu->threads, tcb);
            list_append(zombie_threads, tcb);
            release(&zombie_threads->lock);

            this_cpu->current_tcb = this_cpu->threads->head;
            release(&this_cpu->threads->lock);
            if (cleaner_tcb->state == THREAD_PAUSED)
                cleaner_tcb->state = THREAD_READY;
        }
        
        if (this_cpu->current_tcb == this_cpu->idle_tcb)
            this_cpu->idle_time += this->last_cpu_time;
        this_cpu->total_time += this->last_cpu_time;

        release(&this->lock);
        this_cpu->current_tcb = sched_find_next(this_cpu, now);
        set_tcb((uintptr_t)this_cpu->current_tcb->value);
    } else {
        node_t *node = sched_find_next(get_core_logical(get_logical_id()), now);
        set_tcb((uintptr_t)node->value);
        this_cpu->current_tcb = node;
    }
    acquire(&this->lock);

    this->start_time = now;
    if (this->state == THREAD_READY)
        this->state = THREAD_RUNNING;
    release(&this->lock);

    arch_restore_context(r);
}

void idle(void) {
    for (;;) {
        wfi();
    }
}

void sched_clean_tcb(struct thread *tcb) {
    assert(tcb->refcount <= 1);
    arch_context_free(tcb);
    sched_free_tid(tcb->tid);
    kfree(tcb);
}

void sched_cleaner(void) {
    struct process *last_proc = NULL;

    for (;;) {
        sched_sleep(10000000);

        cli();
        while (!trylock(&zombie_threads->lock))
            sched_yield();
        node_t *node = zombie_threads->head;
        if (!node) {
            release(&zombie_threads->lock);
            sti();
            sched_block(this, 0);
            continue;
        }
        struct thread *tcb = node->value;
        list_unlink(zombie_threads, node);
        release(&zombie_threads->lock);
        sti();

        kfree(node);

        struct process *proc = tcb->parent, *parent = proc->parent;
        acquire(&proc->threads->lock);
        list_remove_value(proc->threads, tcb);

        while (__atomic_load_n(&tcb->cpu->current_tcb->value, __ATOMIC_RELAXED) == tcb);

        if (proc->threads->length == 0) {
            if (init_proc == proc)
                panic("Tried to kill init!");

            acquire(&processes->lock);
            list_remove_value(processes, proc);
            release(&processes->lock);

            if (parent) {
                struct dead_process *dp = kmalloc(sizeof(struct dead_process));
                dp->pid = proc->pid;
                dp->status = proc->exit_status;

                acquire(&parent->dead_children->lock);
                list_insert(parent->dead_children, dp);
                signal_send(parent, SIGCHLD);
                release(&parent->dead_children->lock);
                
                acquire(&parent->children->lock);
                list_remove_value(parent->children, proc);
                release(&parent->children->lock);
            }

            acquire(&proc->children->lock);
            foreach(j, proc->children) {
                struct process *child = j->value;
                child->parent = init_proc;
            }
            list_free(proc->children);

            acquire(&proc->dead_children->lock);
            foreach(j, proc->dead_children) {
                struct dead_process *dp = j->value;
                if (dp)
                    kfree(dp);
            }
            list_free(proc->dead_children);

            vfs_close(proc->cwd);
            vma_destroy(proc->vma, proc->pm);
            mmu_destroy_pagemap(proc->pm);
            kfree(proc->files);
            kfree(proc->name);
            sched_free_pid(proc->pid);
            list_free(proc->threads);
            if (proc->user)
                __atomic_sub_fetch(&user_procs, 1, __ATOMIC_RELEASE);

            if (last_proc)
                kfree(last_proc);
            last_proc = proc;

        } else {
            release(&proc->threads->lock);
        }

        if (__atomic_sub_fetch(&tcb->refcount, 1, __ATOMIC_ACQ_REL) == 0)
            sched_clean_tcb(tcb);
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
        tcb->cpu = core;
        core->idle_tcb = list_insert(core->threads, tcb);
    }

    struct process *cleaner = sched_new_process("psycho killer", false);
    cleaner_tcb = sched_new_thread(cleaner, sched_cleaner, 0, NULL, NULL, NULL, 0, NULL);
    cleaner_tcb->state = THREAD_PAUSED;
    sched_add_process(cleaner);

    dprintf(LOG_INFO, "\033[93msched:\033[0m initialized scheduler\n");
}

void sched_shutdown(void) {
    this_proc->user = false;

    acquire(&processes->lock);
    foreach(i, processes) {
        struct process *proc = i->value;
        if (!proc->user || proc == this_proc || proc == init_proc)
            continue;

        release(&processes->lock);
        sched_exit_group(proc, SIGKILL);
        acquire(&processes->lock);
    }
    release(&processes->lock);

    while (__atomic_load_n(&user_procs, __ATOMIC_ACQUIRE) > ((this_proc == init_proc) ? 1 : 2))
        sched_yield();
}