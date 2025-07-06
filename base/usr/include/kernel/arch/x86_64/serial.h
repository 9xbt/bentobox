#pragma once
#include <kernel/vfs.h>

extern struct vfs_node *serial_redirect;
extern char serial_ringbuffer[];

void serial_install(void);
void serial_puts(char *str);
void serial_redirect_debug();