#pragma once
#include <kernel/vfs.h>

extern struct vfs_node *serial_redirect;

void serial_install(void);
void serial_puts(char *str);
void serial_redirect_debug();