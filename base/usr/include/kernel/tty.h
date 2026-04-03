#pragma once
#include <kernel/spinlock.h>
#include <kernel/termios.h>
#include <kernel/sched.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

typedef struct tty {
    long (*ioctl)(struct vfs_node *node, int op, void *arg);
    void (*flush)(struct vfs_node *node);
    int locked;
    int pgid;
    bool sgr_mode;
    bool mouse_tracking;
    struct termios tio;
    struct winsize ws;
    fifo_t *ififo;
    fifo_t *ofifo;
} tty_t;

long tty_enqueue(vfs_node_t *node, unsigned char c);
long tty_enqueue_string(vfs_node_t *node, const char *s);
void tty_enqueue_sgr_event(vfs_node_t *node, int button, int col, int row, bool release);
tty_t *tty_create(vfs_node_t *node);
void tty_destroy(vfs_node_t *node);
void tty_spawn_worker(void);