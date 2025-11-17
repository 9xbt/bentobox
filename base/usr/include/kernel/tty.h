#pragma once
#include <kernel/spinlock.h>
#include <kernel/termios.h>
#include <kernel/sched.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

typedef struct tty {
    fifo_t *ififo;
    fifo_t *ofifo;
    vfs_node_t *node;
    int pgid;
    struct termios tio;
    struct thread *worker;
    long (*ioctl)(struct vfs_node *node, int op, void *arg);
    bool sgr_mode;
    bool mouse_tracking;
} tty_t;

long tty_enqueue_string(vfs_node_t *node, const char *s);
tty_t *tty_create(vfs_node_t *node);
void tty_destroy(vfs_node_t *node);
void tty_spawn_worker(void);