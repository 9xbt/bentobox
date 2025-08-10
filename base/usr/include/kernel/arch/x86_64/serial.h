#pragma once
#include <kernel/vfs.h>

extern char serial_ringbuffer[1024];
extern int loglevel;

void serial_install(void);
void serial_tty_flush(void);
long serial_tty_enqueue(int c);
long serial_tty_dequeue(bool block);