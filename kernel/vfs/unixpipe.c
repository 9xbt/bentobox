#include <kernel/ringbuffer.h>
#include <kernel/unixpipe.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/file.h>
#include <kernel/vfs.h>

long unixpipe_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    struct unix_pipe *pipe = node->device;
    if (pipe->write_refs <= 0 && ringbuffer_empty(pipe->buffer)) {
        return 0;
    }
    if (pipe->write_refs > 0 && ringbuffer_empty(pipe->buffer)) {
        struct file *file = file_get_from_node(node);
        if (file->flags & O_NONBLOCK)
            return -EAGAIN;
        list_insert(pipe->buffer->waiting_readers, this);
        this->state = THREAD_PAUSED;
        sched_yield();
    }
    return ringbuffer_read(pipe->buffer, buffer, len);
}

long unixpipe_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    struct unix_pipe *pipe = node->device;
    if (pipe->read_refs <= 0) {
        signal_send(this_proc, SIGPIPE);
        sched_yield();
        return -EPIPE;
    }
    if (pipe->read_refs > 0 && ringbuffer_full(pipe->buffer)) {
        struct file *file = file_get_from_node(node);
        if (file->flags & O_NONBLOCK)
            return -EAGAIN;
        list_insert(pipe->buffer->waiting_writers, this);
        this->state = THREAD_PAUSED;
        sched_yield();
    }
    return ringbuffer_write(pipe->buffer, buffer, len);
}

void unixpipe_destroy(struct unix_pipe *pipe) {
    ringbuffer_destroy(pipe->buffer);

    list_free(pipe->read_end->children);
    list_free(pipe->read_end->waiters);
    kfree(pipe->read_end);
    
    list_free(pipe->write_end->children);
    list_free(pipe->write_end->waiters);
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
        foreach_safe(i, pipe->buffer->waiting_readers) {
            struct thread *tcb = i->value;
            tcb->state = THREAD_RUNNING;
            list_remove(pipe->buffer->waiting_readers, i);
        }
    }
    if (pipe->read_refs <= 0 && pipe->write_refs <= 0) {
        unixpipe_destroy(pipe);
    }
    return 0;
}

vfs_ops_t unixpipe_read_ops = {
    .read = unixpipe_read,
    .close = unixpipe_close_read
};

vfs_ops_t unixpipe_write_ops = {
    .write = unixpipe_write,
    .close = unixpipe_close_write
};

int unixpipe_new(int fds[2], int flags) {
    vfs_node_t *pipes[2] = {
        vfs_create_node("[pipe::read]", VFS_UNIXPIPE),
        vfs_create_node("[pipe::write]", VFS_UNIXPIPE)
    };

    fds[0] = file_create(pipes[0], flags);
    fds[1] = file_create(pipes[1], flags);

    struct unix_pipe *device = kmalloc(sizeof(struct unix_pipe));
    device->read_end = pipes[0];
    device->write_end = pipes[1];
    device->read_refs = 1;
    device->write_refs = 1;
    device->buffer = ringbuffer_create(UNIXPIPE_BUFFER_SIZE);
    device->lock = 0;

    pipes[0]->ops = &unixpipe_read_ops;
    pipes[0]->device = device;
    
    pipes[1]->ops = &unixpipe_write_ops;
    pipes[1]->device = device;

    return 0;
}