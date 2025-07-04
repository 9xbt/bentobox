#include <kernel/sched.h>
#include <kernel/signal.h>

void _sigint(struct process *proc) {
    sched_kill(proc, 128 + SIGINT);
}

void _sigpipe(struct process *proc) {
    sched_kill(proc, 128 + SIGPIPE);
}

void _sigchld(struct process *proc) {
    sched_unblock(proc);
}

void signal_send(struct process *proc, int signal, int signal_data) {
    if (!proc || signal < 1 || signal > 32) {
        return;
    }
    
    sched_lock();
    proc->pending_signals |= (1 << (signal - 1));
    proc->signal_data = signal_data;
    proc->state = TASK_SIGNAL;
    sched_unlock();
}