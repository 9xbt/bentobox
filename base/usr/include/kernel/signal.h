#pragma once
#include <kernel/sched.h>

#define SIGINT  2
#define SIGPIPE 13
#define SIGTERM 15
#define SIGCHLD 17

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define _NSIG  65
#define _NSIG_WORDS  ((_NSIG + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long)))

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

void signal_send(struct process *proc, int signal, int data);

void _sigint(struct process *proc);
void _sigpipe(struct process *proc);
void _sigterm(struct process *proc);
void _sigchld(struct process *proc);