#pragma once
#include <limits.h>
#include <kernel/context.h>

#define _NSIG	65
#define _NSIG_WORDS  ((_NSIG + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long)))

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

typedef struct siginfo siginfo_t;

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

struct sigframe {
    unsigned long pretcode;
    struct context ctx;
    int sig;
    sigset_t oldmask;
};

#define sigaddset(set, _sig) do { \
    int _word = ((_sig) - 1) / LONG_BIT; \
    int _bit = ((_sig) - 1) % LONG_BIT; \
    (set)->sig[_word] |= (1UL << _bit); \
} while(0)

#define sigdelset(set, _sig) do { \
    int _word = ((_sig) - 1) / LONG_BIT; \
    int _bit = ((_sig) - 1) % LONG_BIT; \
    (set)->sig[_word] &= ~(1UL << _bit); \
} while(0)

#define sigismember(set, _sig) ({ \
    int _word = ((_sig) - 1) / LONG_BIT; \
    int _bit = ((_sig) - 1) % LONG_BIT; \
    ((set)->sig[_word] >> _bit) & 1; \
})

#define sigemptyset(set) memset((set), 0, sizeof(sigset_t))

#define sigorset(dest, set1, set2) do { \
    for (int _i = 0; _i < _NSIG_WORDS; _i++) \
        (dest)->sig[_i] = (set1)->sig[_i] | (set2)->sig[_i]; \
} while(0)

#define SIGNAL_TRAMPOLINE_BASE  0x7FFFFFFFE000UL

#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SIGHUP   1
#define	SIGINT	 2
#define SIGQUIT  3
#define SIGILL   4
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV  11
#define SIGPIPE  13
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP	 19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGURG   23
#define SIGWINCH 28

#define LONG_BIT (sizeof(long) * CHAR_BIT)
struct thread;
struct process;

int  signal_handle(struct thread *tcb, int sig);
int  signal_send(struct process *proc, int sig);
int  signal_send_pgrp(int pgid, int sig);
void signal_check_pending(struct thread *tcb);