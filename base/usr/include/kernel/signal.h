#pragma once
#include <kernel/sched.h>

#define SIGINT  2
#define SIGPIPE 13
#define SIGCHLD 17

void signal_send(struct process *proc, int signal, int data);

void _sigint(struct process *proc);
void _sigpipe(struct process *proc);
void _sigchld(struct process *proc);