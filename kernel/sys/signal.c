#include <kernel/bitmap.h>
#include <kernel/signal.h>
#include <kernel/printf.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>

extern void arch_setup_signal_frame(struct thread *tcb, struct sigframe *frame, struct sigaction *action, int sig);

static void setup_signal_frame(struct thread *tcb, int sig, struct sigaction *action) {
    struct process *proc = tcb->parent;
    
    uintptr_t *pm = mmu_get_pm();
    mmu_switch_pm(tcb->parent->pm);
    
    struct sigframe *frame = (struct sigframe *)ALIGN_DOWN(tcb->ctx.user_stack - sizeof(struct sigframe), 16);
    frame->sig = sig;
    frame->oldmask = proc->blocked;
    frame->pretcode = SIGNAL_TRAMPOLINE_BASE;
    
    arch_setup_signal_frame(tcb, frame, action, sig);
    
    for (unsigned long i = 0; i < _NSIG_WORDS; i++)
        proc->blocked.sig[i] |= action->sa_mask.sig[i];
    
    if (!(action->sa_flags & SA_NODEFER))
        sigaddset(&proc->blocked, sig);
    
    tcb->sigframe = frame;
    
    mmu_switch_pm(pm);
}

int signal_handle(struct thread *tcb, int sig) {
    struct process *proc = tcb->parent;
    struct sigaction *action = &proc->sighand[sig];

    if (sigismember(&proc->blocked, sig))
        return 1;
    
    int word = (sig - 1) / LONG_BIT;
    int bit = (sig - 1) % LONG_BIT;
    proc->psig.sig[word] &= ~(1UL << bit);
    
    if (action->sa_handler == SIG_IGN)
        return 1;
    if (action->sa_handler == SIG_DFL) {
        switch (sig) {
            case SIGCHLD:
            case SIGURG:
            case SIGWINCH:
                return 0;
            case SIGINT:
            case SIGTERM:
            case SIGKILL:
                sched_exit_group(proc, sig);
                return 0;
            case SIGILL:
                dprintf(LOG_DEBUG, "\033[93m%s:\033[0m Illegal instruction\n", proc->name);
                sched_exit_group(proc, sig);
                return 0;
            case SIGSEGV:
                dprintf(LOG_DEBUG, "\033[93m%s:\033[0m Segmentation fault\n", proc->name);
                sched_exit_group(proc, sig);
                return 0;
            case SIGBUS:
            case SIGFPE:
            case SIGABRT:
            case SIGQUIT:
                sched_exit_group(proc, sig);
                return 0;
            case SIGSTOP:
            case SIGTSTP:
            case SIGTTIN:
            case SIGTTOU:
                if (tcb->parent != init_proc) {
                    acquire(&tcb->lock);
                    tcb->state = THREAD_PAUSED;
                    release(&tcb->lock);
                }
                return 0;
            case SIGCONT:
                acquire(&tcb->lock);
                if (tcb->state == THREAD_PAUSED)
                    tcb->state = THREAD_RUNNING;
                release(&tcb->lock);
                return 0;
            default:
                dprintf(LOG_DEBUG, "\033[93m%s:\033[0m unhandled signal %d\n", proc->name, sig);
                return 0;
        }
    }
    
    if (!tcb->syscall_regs)
        return 1;
    setup_signal_frame(tcb, sig, action);
    return 0;
}

int signal_send(struct process *proc, int sig) {
    if (!proc)
        return -ESRCH;
    if (sig < 1 || sig >= _NSIG)
        return -EINVAL;
    if (!proc->user)
        return -EPERM;

    int word = (sig - 1) / LONG_BIT;
    int bit  = (sig - 1) % LONG_BIT;
    proc->psig.sig[word] |= (1UL << bit);

    struct thread *tcb = proc->threads->head->value;
    if (tcb->state == THREAD_PAUSED || tcb->state == THREAD_SLEEPING)
        tcb->state = THREAD_RUNNING;
    
    return 0;
}

int signal_send_pgrp(int pgid, int sig) {
    int err = -ESRCH;
    foreach_safe(i, processes) {
        struct process *proc = i->value;
        if (proc->pgid == pgid) {
            signal_send(proc, sig);
            err = 0;
        }
    }
    return err;
}

void signal_check_pending(struct thread *tcb) {
    if (!tcb || !tcb->parent || tcb->sigframe)
        return;

    struct process *proc = tcb->parent;
    for (int sig = 1; sig < _NSIG; sig++) {
        if (sigismember(&proc->psig, sig) && !sigismember(&proc->blocked, sig)) {
            if (!signal_handle(tcb, sig))
                return;
        }
    }
}