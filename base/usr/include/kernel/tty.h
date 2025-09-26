#pragma once
#include <stdbool.h>
#include <kernel/vfs.h>

extern vfs_ops_t tty_ops;

long tty_enqueue(int c);
long tty_dequeue(bool block);
void tty_enqueue_string(char *str);