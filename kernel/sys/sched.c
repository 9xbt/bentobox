#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>

extern void create_context(struct context *ctx, void *entry);
extern void destroy_context(struct context *ctx);
extern void jumpstart(void);

struct thread *sched_new_thread(struct process *parent, void *entry) {
    struct thread *tcb = kmalloc(sizeof(struct thread));
    tcb->tid = 0;
    tcb->state = THREAD_NEW;
    create_context(&tcb->ctx, entry);
    tcb->parent = parent;
    
    return tcb;
}

struct process *sched_new_process(void *entry, const char *name) {
    struct process *proc = kmalloc(sizeof(struct process));
    proc->name = strdup(name);
    proc->pm = kernel_pd;
    proc->pid = 0;
    proc->user = false;
    proc->parent = NULL;
    proc->children = list_create();
    proc->threads = list_create();

    list_insert(proc->threads, sched_new_thread(proc, entry));
    
    dprintf(LOG_DEBUG, "\033[93msched:\033[0m created process '%s'\n", name);
    return proc;
}

node_t *sched_add_process(struct cpu *cpu, struct process *proc) {
    foreach(thread, proc->threads) {
        list_insert(cpu->threads, thread->value);
    }
    return list_insert(cpu->processes, proc);
}

void sched_install(void) {
    dprintf(LOG_INFO, "\033[93msched:\033[0m initialized process lists\n");

    jumpstart();
}