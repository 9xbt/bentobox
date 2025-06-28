#pragma once
#include <kernel/sched.h>

#define SIGINT  2
#define SIGCHLD 1

void send_signal(struct process *proc, int signal, int extra);