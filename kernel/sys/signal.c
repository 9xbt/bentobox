#include <kernel/bitmap.h>
#include <kernel/signal.h>
#include <kernel/printf.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>

void signal_handle(struct thread *tcb, int sig) {
    switch (sig) {
        case SIGINT:
            sched_exit(tcb);
            break;
        case SIGILL:
            dprintf(LOG_ERR, "\033[93m%s:\033[0m Illegal instruction\n", tcb->parent->name);
            sched_exit(tcb);
            break;
        case SIGSEGV:
            dprintf(LOG_ERR, "\033[93m%s:\033[0m Segmentation fault\n", tcb->parent->name);
            sched_exit(tcb);
            break;
        case SIGCHLD:
            break;
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m unknown signal %d\n", tcb->parent->name, sig);
            break;
    }
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