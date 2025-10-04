#include <kernel/unixpipe.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/smp.h>
#include <stddef.h>

extern void arch_context_init(struct thread *tcb, void *entry, bool user, int argc, char *argv[], char *envp[]);
extern void arch_context_free(struct thread *tcb);
extern void arch_context_fork(struct thread *tcb);
extern void arch_save_context(void);
extern void arch_restore_context(void);
extern void arch_jumpstart(void);
extern void arch_yield(struct cpu *cpu);

list_t *processes = NULL;

uint8_t *pid_bitmap = NULL;
uint8_t *tid_bitmap = NULL;
size_t last_pid_bit = 0;
size_t last_tid_bit = 0;

struct process *init_proc = NULL;
struct thread  *cleaner_tcb = NULL;

int sched_allocate_pid(void) {
    for (int pid = last_pid_bit; pid < SCHED_BITMAP_SIZE * 8; pid++) {
        if (!bitmap_get(pid_bitmap, pid)) {
            bitmap_set(pid_bitmap, pid);
            return pid;
        }
    }
    return -1;
}

int sched_allocate_tid(void) {
    for (int tid = last_tid_bit; tid < SCHED_BITMAP_SIZE * 8; tid++) {
        if (!bitmap_get(tid_bitmap, tid)) {
            bitmap_set(tid_bitmap, tid);
            return tid;
        }
    }
    return -1;
}

void sched_free_pid(int pid) {
    bitmap_clear(pid_bitmap, pid);
    last_pid_bit = pid;
}

void sched_free_tid(int tid) {
    bitmap_clear(tid_bitmap, tid);
    last_tid_bit = tid;
}

struct cpu *sched_find_cpu(void) {
    static size_t id = 0;
    struct cpu *c = cpu_list[id];
    id = (id + 1) % cpu_count;
    return c;
}

node_t *sched_add_process(struct process *proc) {
    foreach(thread, proc->threads) {
        struct cpu *cpu = sched_find_cpu();
        ((struct thread *)thread->value)->cpu = cpu;
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

struct thread *sched_new_thread(struct process *parent, void *entry, int argc, char *argv[], char *envp[]) {
    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = sched_allocate_tid();
    tcb->state = THREAD_RUNNING;
    tcb->parent = parent;
    tcb->cpu = NULL;
    tcb->syscall_regs = NULL;
    memset(&tcb->psig, 0, sizeof tcb->psig);
    tcb->doing_user_copy = false;
    tcb->user_copy_status = 0;

    static char *empty_argv_envp[] = { NULL };
    arch_context_init(tcb, entry, parent->user, argc, argv ? argv : empty_argv_envp, envp ? envp : empty_argv_envp);
    
    list_insert(parent->threads, tcb);
    return tcb;
}

struct process *sched_new_process(const char *name, bool user) {
    struct process *proc = kmalloc(sizeof(struct process));
    proc->name = strdup(name);
    proc->pm = mmu_create_pagemap();
    proc->pid = sched_allocate_pid();
    proc->pgid = 0;
    proc->user = user;
    proc->state = PROCESS_ALIVE;
    proc->parent = NULL;
    proc->children = list_create();
    proc->threads = list_create();
    proc->vma = vma_create(SCHED_VMA_BASE, SCHED_VMA_SIZE);
    proc->max_files = 16;
    proc->files = kmalloc(sizeof(struct file) * proc->max_files);
    proc->files[0] = proc->files[1] = proc->files[2] = file_new(vfs_open(NULL, "/dev/tty1", 0), 0);
    proc->cwd = NULL;
    memset(&proc->psig, 0, sizeof proc->psig);

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
    proc->user = true;
    proc->state = PROCESS_ALIVE;
    proc->parent = this_proc;
    proc->children = list_create();
    proc->threads = list_create();
    proc->vma = vma_clone(this_proc->vma, proc->pm);

    proc->max_files = this_proc->max_files;
    proc->files = kmalloc(sizeof(struct file) * proc->max_files);
    memcpy(proc->files, this_proc->files, sizeof(struct file) * proc->max_files);
    for (int i = 0; i < proc->max_files; i++) {
        struct file *file = &proc->files[i];
        if (!file->open || file->node->type != VFS_UNIXPIPE)
            continue;
        struct unix_pipe *pipe = file->node->device;
        if (!strcmp(file->node->name, "[pipe::read]"))
            pipe->read_refs++;
        else if (!strcmp(file->node->name, "[pipe::write]"))
            pipe->write_refs++;
    }
    proc->cwd = this_proc->cwd;
    memset(&proc->psig, 0, sizeof proc->psig);

    list_insert(this_proc->children, proc);

    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = sched_allocate_tid();
    tcb->state = THREAD_RUNNING;
    tcb->parent = proc;
    tcb->cpu = NULL;
    arch_context_fork(tcb);
    
    list_insert(proc->threads, tcb);

    dprintf(LOG_DEBUG, "\033[93msched:\033[0m forked process '%s' with pid %d\n", proc->name, proc->pid);
    sched_add_process(proc);
    return proc->pid;
}

void sched_yield(void) {
    arch_yield(this_cpu);
}

void sched_exit(struct thread *tcb) {
    tcb->parent->state = PROCESS_ZOMBIE;
    cleaner_tcb->state = THREAD_RUNNING;
    if (tcb == this) {
        this->state = THREAD_ZOMBIE;
        sched_yield();
        for (;;) {}
    }
}

void sched_exit_group(struct process *proc) {
    proc->state = PROCESS_ZOMBIE_ALL;
    cleaner_tcb->state = THREAD_RUNNING;
    if (proc == this_proc) {
        this->state = THREAD_ZOMBIE;
        sched_yield();
        for (;;) {}
    }
}

void sched_deliver_signals(struct thread *tcb) {
    tcb->psig = this_proc->psig;
    memset(&this_proc->psig, 0, sizeof this_proc->psig);
    for (int sig = 1; sig < _NSIG; sig++) {
        int word = (sig - 1) / LONG_BIT;
        int bit  = (sig - 1) % LONG_BIT;

        if (tcb->psig.sig[word] & (1ul << bit)) {
            tcb->psig.sig[word] &= ~(1ul << bit);

            signal_handle(tcb, sig);
        }
    }
}

node_t *sched_find_next(void) {
    node_t *start = (this_cpu->current_tcb && this_cpu->current_tcb->next) ? this_cpu->current_tcb->next : this_cpu->threads->head, *node = start;
    do {
        struct thread *t = (struct thread *)node->value;
        if (t->state == THREAD_RUNNING)
            return node;

        node = node->next ? node->next : this_cpu->threads->head;
    } while (node != start);

    return this_cpu->idle_tcb;
}

void sched_schedule(struct registers *r) {
    if (this_cpu->current_tcb) {
        if (this->state == THREAD_ZOMBIE)
            __atomic_store_n(&this->state, THREAD_ZOMBIE_ACK, __ATOMIC_SEQ_CST);

        memcpy(&(this->ctx.regs), r, sizeof(struct registers));
        arch_save_context();

        this_cpu->current_tcb = sched_find_next();
    } else {
        this_cpu->current_tcb = sched_find_next();
    }

    this->cpu = this_cpu;
    sched_deliver_signals(this);

    memcpy(r, &(this->ctx.regs), sizeof(struct registers));
    arch_restore_context();
}

void sched_cleaner(void) {
    for (;;) {
        foreach_safe(i, processes) {
            struct process *proc = i->value;
            if (proc->state != PROCESS_ZOMBIE && proc->state != PROCESS_ZOMBIE_ALL)
                continue;
            
            if (proc->state == PROCESS_ZOMBIE) {
                foreach_safe(j, proc->threads) {
                    struct thread *tcb = j->value;
                    if (tcb->state == THREAD_ZOMBIE ||
                        tcb->state == THREAD_ZOMBIE_ACK) {

                        arch_context_free(tcb);
                        sched_free_tid(tcb->tid);
                        list_remove_value(tcb->cpu->threads, tcb);
                        list_remove(proc->threads, j);
                        kfree(tcb);
                    }
                }

                if (proc->threads->length > 0)
                    continue;
            }

            dprintf(LOG_DEBUG, "\033[93msched:\033[0m reaping %s\n", proc->name);

            if (init_proc == proc)
                init_proc = NULL;

            if (proc->parent) {
                signal_send(proc->parent, SIGCHLD);
                list_remove_value(proc->parent->children, proc);
            }

            foreach(j, proc->threads) {
                struct thread *tcb = j->value;
                if (tcb->cpu->current_tcb->value == tcb) {
                    tcb->state = THREAD_ZOMBIE;
                    arch_yield(tcb->cpu);
                    while (__atomic_load_n(&tcb->state, __ATOMIC_ACQUIRE) != THREAD_ZOMBIE_ACK) {
                        #ifdef __x86_64__
                        __builtin_ia32_pause();
                        #endif
                    }
                }
                arch_context_free(tcb);
                sched_free_tid(tcb->tid);
                list_remove_value(tcb->cpu->threads, tcb);
                kfree(tcb);
            }
            list_free(proc->threads);

            foreach(j, proc->children) {
                struct process *child = j->value;
                child->parent = init_proc;
            }
            list_free(proc->children);

            for (int j = 0; j < proc->max_files; j++) {
                struct file *file = &proc->files[j];
                if (file->open)
                    vfs_close(file->node);
            }

            vma_destroy(proc->vma, proc->pm);
            mmu_destroy_pagemap(proc->pm);
            kfree(proc->files);
            kfree(proc->name);
            sched_free_pid(proc->pid);

            list_remove(processes, i);
            kfree(proc);

            mmu_print_memory();
        }

        this->state = THREAD_PAUSED;
        sched_yield();
    }
}

void sched_install(void) {
    processes  = list_create();
    pid_bitmap = kmalloc(SCHED_BITMAP_SIZE);
    tid_bitmap = kmalloc(SCHED_BITMAP_SIZE);
    memset(pid_bitmap, 0, SCHED_BITMAP_SIZE);
    memset(tid_bitmap, 0, SCHED_BITMAP_SIZE);

    struct process *cleaner = sched_new_process("psycho killer", false);
    cleaner_tcb = sched_new_thread(cleaner, sched_cleaner, 0, NULL, NULL);
    cleaner_tcb->state = THREAD_PAUSED;
    sched_add_process(cleaner);

    dprintf(LOG_INFO, "\033[93msched:\033[0m initialized scheduler\n");
}