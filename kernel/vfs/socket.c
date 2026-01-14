#include <kernel/socket.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/assert.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

long local_socket_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    struct socket *sock = node->device;
    if (sock->domain != PF_LOCAL)
        return -EOPNOTSUPP;
    
    acquire(sock->lock);
    if (!sock->peer || sock->state != SOCKET_CONNECTED) {
        release(sock->lock);
        return -ENOTCONN;
    }
    if (sock->peer->recv_queue->length >= SOCKET_MAX_QUEUE_ENTRIES) {
        release(sock->lock);
        return -EAGAIN;
    }
    
    struct socket_buffer *buf = kmalloc(sizeof(struct socket_buffer));
    buf->data = kmalloc(len);
    buf->len = len;
    buf->offset = 0;
    memcpy(buf->data, buffer, len);
    
    list_insert(sock->peer->recv_queue, buf);
    release(sock->lock);
    
    vfs_wake_waiters(sock->peer->fd_node);
    vfs_wake_waiters(sock->peer->node);
    
    return len;
}

long local_socket_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    struct socket *sock = node->device;
    if (sock->domain != PF_LOCAL)
        return -EOPNOTSUPP;

    acquire(sock->lock);
    if (sock->state != SOCKET_CONNECTED) {
        release(sock->lock);
        return -ENOTCONN;
    }
    if (!sock->recv_queue->length) {
        release(sock->lock);
        return -EAGAIN;
    }

    struct socket_buffer *buf = sock->recv_queue->head->value;
    
    size_t n = (buf->len - buf->offset) < len ? (buf->len - buf->offset) : len;
    memcpy(buffer, buf->data + buf->offset, n);
    
    buf->offset += n;
    
    if (buf->offset >= buf->len) {
        list_pop(sock->recv_queue);
        kfree(buf->data);
        kfree(buf);
        if (sock->peer) {
            vfs_wake_waiters(sock->peer->fd_node);
            vfs_wake_waiters(sock->peer->node);
        }
    }
    release(sock->lock);
    
    return n;
}

long socket_poll(vfs_node_t *node, long events) {
    struct socket *sock = node->device;
    long revents = 0;
    
    acquire(sock->lock);
    if (events & POLLIN) {
        if (sock->state == SOCKET_LISTENING && sock->pending->length > 0)
            revents |= POLLIN;
        if (sock->state == SOCKET_CONNECTED && sock->recv_queue->length > 0)
            revents |= POLLIN;
    }
    if (events & POLLOUT) {
        if (sock->state == SOCKET_CONNECTED && sock->peer && sock->peer->recv_queue->length < SOCKET_MAX_QUEUE_ENTRIES)
            revents |= POLLOUT;
    }
    release(sock->lock);
    
    return revents;
}

long socket_remove(vfs_node_t *node) {
    struct socket *sock = node->device;
    if (!sock)
        return 0;
    
    if (sock->peer) {
        sock->peer->peer = NULL;
        if (sock->peer->node)
            vfs_wake_waiters(sock->peer->node);
    }

    if (sock->node && sock->node != node && sock->state == SOCKET_LISTENING)
        sock->node->device = NULL;

    if (sock->pending)
        list_free(sock->pending);

    while (sock->recv_queue->length > 0) {
        struct socket_buffer *buf = list_pop(sock->recv_queue);
        if (buf) {
            kfree(buf->data);
            kfree(buf);
        }
    }

    list_free(sock->recv_queue);
    kfree(sock);

    node->device = NULL;
    return 0;
}

vfs_ops_t local_socket_ops = {
    .read = local_socket_read,
    .write = local_socket_write,
    .remove = socket_remove,
    .poll = socket_poll
};

static struct socket *socket_create(int domain, int type) {
    struct socket *sock = kmalloc(sizeof(struct socket));
    sock->domain = domain;
    sock->type = type;
    sock->backlog = 0;
    sock->state = SOCKET_NONE;
    sock->pending = list_create();
    sock->recv_queue = list_create();
    sock->peer = NULL;
    sock->node = NULL;
    sock->fd_node = NULL;
    sock->lock = kmalloc(sizeof(spinlock_t));
    *sock->lock = 0;
    return sock;
}

int socket_new(int domain, int type, int protocol) {
    if (protocol && protocol != SOCK_STREAM)
        return -ENOSYS;

    vfs_node_t *node = vfs_create_node("[socket]", VFS_SOCKET);
    struct socket *sock = socket_create(domain, type);
    sock->node = node;
    sock->fd_node = node;
    node->device = sock;

    int flags = 0;
    if (type & SOCK_CLOEXEC)
        flags |= O_CLOEXEC;
    if (type & SOCK_NONBLOCK)
        flags |= O_NONBLOCK;
    int fd = file_create(node, flags);

    switch (domain) {
        case PF_LOCAL:
            node->ops = &local_socket_ops;
            break;
        default:
            dprintf(LOG_DEBUG, "\033[93mvfs:\033[0m unknown socket domain %d\n", domain);

            file_close(fd);
            kfree(node->device);
            vfs_remove(node);
            return -EINVAL;
    }
    return fd;
}

int socket_bind(int fd, const void *addr, uint32_t addrlen) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->device)
        return -EINVAL;

    struct socket *sock = file->node->device;
    switch (sock->domain) {
        case PF_LOCAL: {
            struct sockaddr_un sa = {0};
            if (copy_from_user(&sa, addr, addrlen) < 0)
                return -EFAULT;

            vfs_node_t *bind = vfs_lookup(this_proc->cwd, sa.sun_path, true, VFS_SOCKET);
            if (!bind)
                return -EINVAL;
            bind->ops = &local_socket_ops;
            bind->device = sock;
            sock->node = bind;
            return 0;
        }
        default:
            dprintf(LOG_DEBUG, "\033[93mvfs:\033[0m unknown socket domain %d\n", sock->domain);
            return -EINVAL;
    }
}

int socket_listen(int fd, int backlog) {
    (void)backlog;
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->device)
        return -EINVAL;

    struct socket *sock = file->node->device;
    sock->backlog = backlog;
    sock->state = SOCKET_LISTENING;
    return 0;
}

int socket_connect(int fd, const void *addr, uint32_t addrlen) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->device)
        return -EINVAL;
    
    struct socket *sock = file->node->device;
    
    switch (sock->domain) {
        case PF_LOCAL: {
            struct sockaddr_un sa;
            if (copy_from_user(&sa, addr, addrlen) < 0)
                return -EFAULT;
            
            vfs_node_t *bind = vfs_lookup(this_proc->cwd, sa.sun_path, true, VFS_NONE);
            if (!bind)
                return -ENOENT;
            
            struct socket *server_sock = bind->device;
            if (!server_sock || server_sock->state != SOCKET_LISTENING)
                return -ECONNREFUSED;
            
            struct socket *server_child = socket_create(sock->domain, sock->type);
            server_child->state = SOCKET_CONNECTED;
            server_child->peer = sock;
            kfree((void *)server_child->lock);
            server_child->lock = sock->lock;
            
            sock->peer = server_child;
            sock->state = SOCKET_CONNECTED;
            
            list_insert(server_sock->pending, server_child);
            vfs_wake_waiters(server_sock->fd_node);
            vfs_wake_waiters(server_sock->node);
            return 0;
        }
        default:
            return -EAFNOSUPPORT;
    }
}

int socket_accept(int fd, const void *addr, uint32_t *addrlen) {
    (void)addr;
    (void)addrlen;
    // dprintf(LOG_DEBUG, "\033[93m%s:\033[0m address is ignored\n", __func__);

    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->device)
        return -EINVAL;

    struct socket *sock = file->node->device;
    if (sock->state != SOCKET_LISTENING)
        return -EINVAL;

    struct socket *client_sock = list_pop(sock->pending);
    if (!client_sock)
        return -EAGAIN;

    vfs_node_t *node = vfs_create_node("[socket]", VFS_SOCKET);
    node->ops = &local_socket_ops;
    node->device = client_sock;
    client_sock->node = node;
    client_sock->fd_node = node;

    int flags = 0;
    if (sock->type & SOCK_CLOEXEC)
        flags |= O_CLOEXEC;
    if (sock->type & SOCK_NONBLOCK)
        flags |= O_NONBLOCK;
    return file_create(node, flags);
}

int socket_getsockopt(int fd, int level, int optname, void *optval, uint32_t *optlen) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->device)
        return -EINVAL;

    // struct socket *sock = file->node->device;

    uint32_t len;
    if (copy_from_user(&len, optlen, sizeof(uint32_t)) < 0)
        return -EFAULT;

    if (level == SOL_SOCKET) {
        switch (optname) {
            case SO_SNDBUF:
            case SO_RCVBUF: {
                int bufsize = 65536;
                if (len < sizeof(int))
                    return -EINVAL;
                if (copy_to_user(optval, &bufsize, sizeof(int)) < 0)
                    return -EFAULT;
                len = sizeof(int);
                if (copy_to_user(optlen, &len, sizeof(uint32_t)) < 0)
                    return -EFAULT;
                return 0;
            }
            default:
                dprintf(LOG_DEBUG, "\033[93mvfs:\033[0m unsupported optname %d\n", optname);
                return -ENOPROTOOPT;
        }
    }

    return -ENOSYS;
}