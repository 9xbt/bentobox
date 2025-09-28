#include <kernel/bitmap.h>
#include <kernel/signal.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>

void signal_handle(struct thread *tcb, int sig) {
    (void)tcb;
    (void)sig;
}

long signal_send(struct process *proc, int sig) {
    if (sig < 1 || sig >= _NSIG)
        return -EINVAL;

    int word = (sig - 1) / LONG_BIT;
    int bit  = (sig - 1) % LONG_BIT;
    proc->psig.sig[word] |= (1UL << bit);

    struct thread *tcb = proc->threads->head->value;
    tcb->state = THREAD_RUNNING;
    
    return 0;
}