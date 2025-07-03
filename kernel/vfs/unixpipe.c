#include <errno.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/fifo.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/unixpipe.h>

long unixpipe_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    dprintf("unixpipe read\n");

    struct unix_pipe *pipe = node->device;
    char *buf = (char *)buffer;
    int i = 0, c;

    while (i < (int)len) {
        while (fifo_is_empty(&pipe->buffer)) {
            if (pipe->write_closed)
                return i == 0 ? 0 : i;
            sched_yield();
        }
        if (fifo_dequeue(&pipe->buffer, &c)) {
            buf[i++] = c;
        }
    }
    return i;
}

long unixpipe_write(vfs_node_t *node, void *buffer, long offset, size_t len) {
    dprintf("unixpipe write\n");

    struct unix_pipe *pipe = node->device;
    char *buf = (char *)buffer;
    int i = 0;
    if (pipe->read_closed) {
        return -EPIPE;
    }
    while (i < (int)len) {
        while (fifo_is_full(&pipe->buffer)) {
            if (pipe->read_closed)
                return -EPIPE;
            sched_yield();
        }
        fifo_enqueue(&pipe->buffer, buf[i++]);
    }
    return i;
}

long unixpipe_close_read(vfs_node_t *node) {
    struct unix_pipe *pipe = node->device;
    pipe->read_closed = true;
    return 0;
}

long unixpipe_close_write(vfs_node_t *node) {
    struct unix_pipe *pipe = node->device;
    pipe->write_closed = true;
    return 0;
}

int unixpipe_new(int fds[2]) {
    vfs_node_t *pipes[2] = {
        vfs_create_node("[pipe::read]", VFS_NONE),
        vfs_create_node("[pipe::write]", VFS_NONE)
    };

    fds[0] = fd_create(pipes[0], 0);
    fds[1] = fd_create(pipes[1], 0);

    struct unix_pipe *device = kmalloc(sizeof(struct unix_pipe));
    device->read_end = pipes[0];
    device->write_end = pipes[1];
    fifo_init(&device->buffer, 1024);

    pipes[0]->read = unixpipe_read;
    pipes[0]->write = unixpipe_write;
    pipes[0]->close = unixpipe_close_read;
    pipes[0]->device = device;
    pipes[1]->read = unixpipe_read;
    pipes[1]->write = unixpipe_write;
    pipes[1]->close = unixpipe_close_write;
    pipes[1]->device = device;

    return 0;
}