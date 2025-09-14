#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>

extern void arch_context_init(struct thread *tcb, void *entry, bool user);
extern void arch_context_free(struct thread *tcb);
extern void arch_save_context(void);
extern void arch_restore_context(void);
extern void arch_jumpstart(void);

node_t *sched_add_process(struct cpu *cpu, struct process *proc) {
    foreach(thread, proc->threads) {
        list_insert(cpu->threads, thread->value);
    }
    return list_insert(cpu->processes, proc);
}

struct thread *sched_new_thread(struct process *parent, void *entry) {
    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = 0;
    tcb->state = THREAD_NEW;
    tcb->parent = parent;
    arch_context_init(tcb, entry, parent->user);
    
    return tcb;
}

struct process *sched_new_process(void *entry, const char *name, bool user) {
    struct process *proc = kmalloc(sizeof(struct process));
    proc->name = strdup(name);
    proc->pm = mmu_create_pagemap();
    proc->pid = 0;
    proc->user = user;
    proc->parent = NULL;
    proc->children = list_create();
    proc->threads = list_create();

    list_insert(proc->threads, sched_new_thread(proc, entry));
    
    dprintf(LOG_DEBUG, "\033[93msched:\033[0m created process '%s'\n", name);
    return proc;
}

void sched_schedule(struct registers *r) {
    if (this_cpu->current_tcb) {
        if (this->state != THREAD_NEW) {
            memcpy(&(this->ctx.regs), r, sizeof(struct registers));
            arch_save_context();
        } else {
            this->state = THREAD_RUNNING;
        }
    } else {
        this_cpu->current_tcb = this_cpu->threads->head;
    }

    if (this_cpu->current_tcb->next)
        this_cpu->current_tcb = this_cpu->current_tcb->next;
    else
        this_cpu->current_tcb = this_cpu->threads->head;

    memcpy(r, &(this->ctx.regs), sizeof(struct registers));
    arch_restore_context();
}

void sched_install(void) {
    arch_jumpstart();
}