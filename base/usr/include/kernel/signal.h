#pragma once
#include <limits.h>

#define _NSIG	65
#define _NSIG_WORDS  ((_NSIG + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long)))

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

#define	SIGINT	2
#define SIGQUIT 3
#define SIGCHLD 17
#define SIGTSTP 20

#define LONG_BIT (sizeof(long) * CHAR_BIT)
struct thread;
struct process;

void signal_handle(struct thread *tcb, int sig);
long signal_send(struct process *proc, int sig);