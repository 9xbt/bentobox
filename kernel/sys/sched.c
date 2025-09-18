#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>

extern void arch_context_init(struct thread *tcb, void *entry, bool user);
extern void arch_context_free(struct thread *tcb);
extern void arch_save_context(void);
extern void arch_restore_context(void);
extern void arch_jumpstart(void);

list_t *processes = NULL;

uint8_t *pid_bitmap = NULL;
uint8_t *tid_bitmap = NULL;
size_t last_pid_bit = 0;
size_t last_tid_bit = 0;

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
        if (!bitmap_get(pid_bitmap, tid)) {
            bitmap_set(pid_bitmap, tid);
            return tid;
        }
    }
    return -1;
}

struct cpu *sched_find_cpu(void) {
    static size_t id = 0;
    if (id >= cpu_count) id = 0;
    return cpu_list[id];
}

node_t *sched_add_process(struct process *proc) {
    foreach(thread, proc->threads) {
        list_insert(sched_find_cpu()->threads, thread->value);
    }
    return list_insert(processes, proc);
}

struct thread *sched_new_thread(struct process *parent, void *entry) {
    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = sched_allocate_tid();
    tcb->state = THREAD_NEW;
    tcb->parent = parent;
    arch_context_init(tcb, entry, parent->user);
    
    list_insert(parent->threads, tcb);
    return tcb;
}

struct process *sched_new_process(const char *name, bool user) {
    struct process *proc = kmalloc(sizeof(struct process));
    proc->name = strdup(name);
    proc->pm = mmu_create_pagemap();
    proc->pid = sched_allocate_pid();
    proc->user = user;
    proc->parent = NULL;
    proc->children = list_create();
    proc->threads = list_create();
    proc->vma = vma_create(0x555555554000, 256 * 1024 * 1024);
    proc->max_files = 16;
    proc->files = kmalloc(sizeof(struct file) * proc->max_files);
    proc->files[0] = proc->files[1] = proc->files[2] = file_new(vfs_open(NULL, "/dev/console", 0), 0);

    dprintf(LOG_DEBUG, "\033[93msched:\033[0m created process '%s'\n", name);
    return proc;
}

node_t *sched_find_next(void) {
    if (this_cpu->current_tcb->next)
        return this_cpu->current_tcb->next;
    else
        return this_cpu->threads->head;
}

void sched_schedule(struct registers *r) {
    if (this_cpu->current_tcb) {
        if (this->state != THREAD_NEW) {
            memcpy(&(this->ctx.regs), r, sizeof(struct registers));
            arch_save_context();
        } else {
            this->state = THREAD_RUNNING;
        }
        this_cpu->current_tcb = sched_find_next();
    } else {
        this_cpu->current_tcb = this_cpu->threads->head;
        if (this->state == THREAD_NEW)
            this->state = THREAD_RUNNING;
    }
    memcpy(r, &(this->ctx.regs), sizeof(struct registers));
    arch_restore_context();
}

void sched_install(void) {
    processes = list_create();

    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    pid_bitmap = vmalloc(kernel_vma, kernel_pd, SCHED_BITMAP_SIZE / PAGE_SIZE, flags);
    tid_bitmap = vmalloc(kernel_vma, kernel_pd, SCHED_BITMAP_SIZE / PAGE_SIZE, flags);

    dprintf(LOG_INFO, "\033[93msched:\033[0m initialized scheduler\n");
}