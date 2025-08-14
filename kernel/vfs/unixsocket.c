#include <fcntl.h>
#include <kernel/socket.h>
#include <kernel/printf.h>
#include <kernel/fd.h>

long unixsocket_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    return 0;
}

long unixsocket_write(vfs_node_t *node, void *buffer, long offset, size_t len) {
    return 0;
}

int unixsocket_new(int type) {
    struct vfs_node *socket = vfs_create_node("[socket]", VFS_SOCKET);
    int flags = 0;
    if (type & SOCK_CLOEXEC)
        flags |= O_CLOEXEC;
    if (type & SOCK_NONBLOCK)
        flags |= O_NONBLOCK;
    int fd = fd_create(socket, flags);
    
    socket->read = unixsocket_read;
    socket->write = unixsocket_write;

    dprintf(LOG_DEBUG, "%s:%d: created AF_UNIX socket at fd %d\n", __FILE__, __LINE__, fd);
    return fd;
}