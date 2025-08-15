#include <errno.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <kernel/ringbuffer.h>
#include <kernel/unixpipe.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/vfs.h>
#include <kernel/fd.h>

long unixpipe_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    struct unix_pipe *pipe = node->device;
    if (pipe->write_refs <= 0 && ringbuffer_empty(pipe->buffer)) {
        return 0;
    }
    if (pipe->write_refs > 0 && ringbuffer_empty(pipe->buffer)) {
        struct fd *fd = fd_get_from_node(node);
        if (fd->flags & O_NONBLOCK)
            return -EAGAIN;
        list_insert(pipe->buffer->waiting_readers, this);
        sched_block(TASK_PAUSED);
    }
    return ringbuffer_read(pipe->buffer, buffer, len);
}

long unixpipe_write(vfs_node_t *node, void *buffer, long offset, size_t len) {
    struct unix_pipe *pipe = node->device;
    if (pipe->read_refs <= 0) {
        signal_send(this, SIGPIPE, 0);
        sched_yield();
        return -EPIPE;
    }
    if (pipe->read_refs > 0 && ringbuffer_full(pipe->buffer)) {
        struct fd *fd = fd_get_from_node(node);
        if (fd->flags & O_NONBLOCK)
            return -EAGAIN;
        list_insert(pipe->buffer->waiting_writers, this);
        sched_block(TASK_PAUSED);
    }
    return ringbuffer_write(pipe->buffer, buffer, len);
}

void unixpipe_destroy(struct unix_pipe *pipe) {
    ringbuffer_destroy(pipe->buffer);
    kfree(pipe->read_end);
    kfree(pipe->write_end);
    kfree(pipe);
}

long unixpipe_close_read(vfs_node_t *node) {
    struct unix_pipe *pipe = node->device;
    if (pipe->read_refs > 0) {
        pipe->read_refs--;
    }
    if (pipe->read_refs <= 0 && pipe->write_refs <= 0) {
        unixpipe_destroy(pipe);
    }
    return 0;
}

long unixpipe_close_write(vfs_node_t *node) {
    struct unix_pipe *pipe = node->device;
    if (pipe->write_refs > 0) {
        pipe->write_refs--;
    }
    if (pipe->write_refs <= 0) {
        foreach(node_item, pipe->buffer->waiting_readers) {
            sched_unblock(node_item->value);
            list_remove(pipe->buffer->waiting_readers, node_item);
        }
    }
    if (pipe->read_refs <= 0 && pipe->write_refs <= 0) {
        unixpipe_destroy(pipe);
    }
    return 0;
}

int unixpipe_new(int fds[2], int flags) {
    vfs_node_t *pipes[2] = {
        vfs_create_node("[pipe::read]", VFS_UNIXPIPE),
        vfs_create_node("[pipe::write]", VFS_UNIXPIPE)
    };

    fds[0] = fd_create(pipes[0], flags);
    fds[1] = fd_create(pipes[1], flags);

    struct unix_pipe *device = kmalloc(sizeof(struct unix_pipe));
    device->read_end = pipes[0];
    device->write_end = pipes[1];
    device->read_refs = 1;
    device->write_refs = 1;
    device->buffer = ringbuffer_create(UNIXPIPE_BUFFER_SIZE);

    pipes[0]->read = unixpipe_read;
    pipes[0]->write = NULL;
    pipes[0]->close = unixpipe_close_read;
    pipes[0]->device = device;
    
    pipes[1]->read = NULL;
    pipes[1]->write = unixpipe_write;
    pipes[1]->close = unixpipe_close_write;
    pipes[1]->device = device;

    return 0;
}

long fifo_close_read(vfs_node_t *node) {
    struct unix_pipe *pipe = node->device;
    if (pipe->read_refs > 0) {
        pipe->read_refs--;
    }
    return 0;
}

long fifo_close_write(vfs_node_t *node) {
    struct unix_pipe *pipe = node->device;
    if (pipe->write_refs > 0) {
        pipe->write_refs--;
    }
    if (pipe->write_refs <= 0) {
        foreach(node_item, pipe->buffer->waiting_readers) {
            sched_unblock(node_item->value);
            list_remove(pipe->buffer->waiting_readers, node_item);
        }
    }
    return 0;
}

vfs_node_t *fifo_open(vfs_node_t *node, int flags) {
    struct unix_pipe *pipe = node->device;
    switch (flags & O_ACCMODE) {
        case O_RDONLY:
            pipe->read_refs++;
            return pipe->read_end;
        case O_WRONLY:
            pipe->write_refs++;
            return pipe->write_end;
        default:
            dprintf(LOG_NOTICE, "%s:%d: R/W FIFOs are not supported\n", __FILE__, __LINE__);
            return NULL;
    }
}

long fifo_poll(struct vfs_node *node, long events) {
    struct unix_pipe *pipe = node->device;
    if (events & POLLIN && !ringbuffer_empty(pipe->buffer)) {
        return POLLIN;
    }
    if (events & POLLOUT && !ringbuffer_full(pipe->buffer)) {
        return POLLOUT;
    }
    return 0;
}

int fifo_new(const char *pathname) {
    vfs_node_t *pipes[2] = {
        vfs_create_node("[pipe::read]", VFS_UNIXPIPE),
        vfs_create_node("[pipe::write]", VFS_UNIXPIPE)
    };

    struct unix_pipe *device = kmalloc(sizeof(struct unix_pipe));
    device->read_end = pipes[0];
    device->write_end = pipes[1];
    device->read_refs = 0;
    device->write_refs = 0;
    device->buffer = ringbuffer_create(UNIXPIPE_BUFFER_SIZE);

    pipes[0]->read = unixpipe_read;
    pipes[0]->write = NULL;
    pipes[0]->close = unixpipe_close_read;
    pipes[0]->device = device;
    pipes[0]->poll = fifo_poll;
    
    pipes[1]->read = NULL;
    pipes[1]->write = unixpipe_write;
    pipes[1]->close = unixpipe_close_write;
    pipes[1]->device = device;
    pipes[1]->poll = fifo_poll;

    vfs_node_t *node = vfs_open(this->cwd, pathname, true, false);
    node->type = VFS_UNIXPIPE;
    node->open = fifo_open;
    node->close = NULL;
    node->device = device;

    return 0;
}