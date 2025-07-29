#include <sys/fcntl.h>
#include <errno.h>
#include <kernel/ringbuffer.h>
#include <kernel/unixpipe.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/vfs.h>
#include <kernel/fd.h>

long unixpipe_write(vfs_node_t *node, void *buffer, long offset, size_t len) {
    struct unix_pipe *pipe = node->device;
    unsigned char *buf = (unsigned char *)buffer;
    int i = 0;
    
    if (pipe->read_refs <= 0)
        signal_send(this, SIGPIPE, 0);
    
    while (i < (int)len) {
        if (pipe->read_refs <= 0) {
            signal_send(this, SIGPIPE, 0);
            return i > 0 ? i : -EPIPE;
        }

        size_t written = ringbuffer_write(pipe->buffer, &buf[i], len - i);
        if (written == 0) {
            sched_yield();
            continue;
        }
        i += written;
    }
    return i;
}

long unixpipe_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    struct unix_pipe *pipe = node->device;
    unsigned char *buf = (unsigned char *)buffer;
    int i = 0;
    
    while (i < (int)len) {
        if (ringbuffer_empty(pipe->buffer)) {
            if (pipe->write_refs <= 0)
                return i;
            sched_yield();
            continue;
        }

        size_t read = ringbuffer_read(pipe->buffer, &buf[i], len - i);
        i += read;
    }
    return i;
}

void unixpipe_destroy(struct unix_pipe *pipe) {
    kfree(pipe->buffer->buffer);
    kfree(pipe->buffer);
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