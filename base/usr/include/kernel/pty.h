#pragma once
#include <kernel/termios.h>
#include <kernel/fifo.h>
#include <kernel/vfs.h>
#include <kernel/tty.h>

#define PTY_BITMAP_SIZE 512

typedef struct pty {
    vfs_node_t *master;
    vfs_node_t *slave;
    int id;
    int locked;
    int pgid;
    struct termios tio;
    struct winsize ws;
    fifo_t *ififo;
    fifo_t *ofifo;
} pty_t;