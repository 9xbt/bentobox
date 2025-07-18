#include <kernel/signal.h>
#include <kernel/sched.h>

void _sigint(struct process *proc) {
    sched_kill(proc, 128 + SIGINT);
}

void _sigpipe(struct process *proc) {
    sched_kill(proc, 128 + SIGPIPE);
}

void _sigterm(struct process *proc) {
    sched_kill(proc, 128 + SIGTERM);
}

void _sigchld(struct process *proc) {
    sched_unblock(proc);
}

void signal_send(struct process *proc, int signal, int signal_data) {
    if (!proc || signal < 1 || signal > 32) {
        return;
    }

    uint32_t sig_bit = 1U << (signal - 1);
    if (proc->signal_mask & sig_bit) {
        return;
    }

    sched_lock();
    proc->pending_signals |= sig_bit;
    proc->signal_data = signal_data;
    proc->state = TASK_SIGNAL;
    sched_unlock();
}