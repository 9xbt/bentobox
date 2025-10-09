#pragma once
#include <kernel/termios.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

typedef struct tty {
    fifo_t *fifo;
    vfs_node_t *node;
    int pgid;
    struct termios tio;
} tty_t;

tty_t *tty_create(vfs_node_t *node);
void tty_destroy(vfs_node_t *node);