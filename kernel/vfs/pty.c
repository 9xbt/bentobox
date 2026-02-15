#include <kernel/spinlock.h>
#include <kernel/termios.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/sched.h>
#include <kernel/errno.h>
#include <kernel/fifo.h>
#include <kernel/pty.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>

uint8_t *pty_bitmap = NULL;
spinlock_t pty_bitmap_lock = 0;

vfs_node_t *ptmx_open(vfs_node_t *node, int flags);

int pty_allocate_id(void) {
    acquire(&pty_bitmap_lock);
    for (int id = 0; id < PTY_BITMAP_SIZE * 8; id++) {
        if (!bitmap_get(pty_bitmap, id)) {
            bitmap_set(pty_bitmap, id);
            release(&pty_bitmap_lock);
            return id;
        }
    }
    release(&pty_bitmap_lock);
    return -1;
}

void pty_free_id(int id) {
    acquire(&pty_bitmap_lock);
    bitmap_clear(pty_bitmap, id);
    release(&pty_bitmap_lock);
}

pty_t *pty_create(void) {
    pty_t *pty = kmalloc(sizeof(pty_t));
    pty->master = NULL;
    pty->slave = NULL;
    pty->id = pty_allocate_id();
    if (pty->id == -1) {
        kfree(pty);
        return NULL;
    }
    pty->tio.c_iflag = BRKINT | ICRNL | IXON;
    pty->tio.c_oflag = OPOST | ONLCR;
    pty->tio.c_cflag = CS8 | CREAD;
    pty->tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    pty->tio.c_cc[VINTR] = 3;
    pty->tio.c_cc[VQUIT] = 28;
    pty->tio.c_cc[VERASE] = 127;
    pty->tio.c_cc[VKILL] = 21;
    pty->tio.c_cc[VEOF] = 4;
    pty->tio.c_cc[VTIME] = 0;
    pty->tio.c_cc[VMIN] = 1;
    pty->tio.c_cc[VSTART] = 17;
    pty->tio.c_cc[VSTOP] = 19;
    pty->tio.c_cc[VSUSP] = 26;
    memset(&pty->ws, 0, sizeof pty->ws);
    pty->locked = 1;
    pty->pgid = 0;
    pty->ififo = fifo_create(4096, char);
    pty->ofifo = fifo_create(4096, char);
    return pty;
}

void pty_destroy(pty_t *pty) {
    if (!pty)
        return;
    
    if (pty->ififo)
        fifo_destroy(pty->ififo);
    if (pty->ofifo)
        fifo_destroy(pty->ofifo);

    pty_free_id(pty->id);
    devfs_remove_numbered(DEVFS_PTY, pty->slave);
    vfs_remove_node(pty->master);
    kfree(pty);
}

vfs_node_t *slave_open(vfs_node_t *node, int flags) {
    (void)flags;
    pty_t *pty = node->device;
    if (!pty)
        return NULL;
    if (pty->locked)
        return NULL;
    return node;
}

long slave_close(vfs_node_t *node) {
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;
    return 0;
}

long master_close(vfs_node_t *node) {
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;

    if (node->refcount <= 1) {
        // TODO
        // pty_destroy(pty);
    }
    return 0;
}

long slave_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;
    if (fifo_is_full(pty->ofifo))
        return -EAGAIN;

    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (pty->tio.c_oflag & OPOST) {
            char c = buf[i];
            if ((pty->tio.c_oflag & ONLCR) && c == '\n') {
                if (fifo_enqueue(pty->ofifo, '\r') < 0)
                    break;
            }
        }

        if (fifo_enqueue(pty->ofifo, buf[i]) < 0)
            break;
    }

    if (i > 0)
        vfs_wake_waiters(pty->master);
    return i;
}

long slave_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;
    if (fifo_is_empty(pty->ififo))
        return -EAGAIN;

    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (fifo_dequeue(pty->ififo, &buf[i]) < 0)
            break;
        if (pty->tio.c_lflag & ECHO && fifo_enqueue(pty->ofifo, buf[i]) < 0)
            break;
    }
    
    if (i > 0)
        vfs_wake_waiters(pty->master);
    return i;
}

long slave_poll(vfs_node_t *node, long events) {
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;

    long revents = 0;
    if (events & POLLIN && !fifo_is_empty(pty->ififo))
        revents |= POLLIN;
    if (events & POLLOUT && !fifo_is_full(pty->ofifo))
        revents |= POLLOUT;
    return revents;
}

long master_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;
    if (fifo_is_full(pty->ififo))
        return -EAGAIN;

    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        switch (buf[i]) {
            case 0x03:
                signal_send_pgrp(pty->pgid, SIGINT);
                break;
            case 0x1A:
                signal_send_pgrp(pty->pgid, SIGTSTP);
                break;
            case 0x0C:
                break;
            case 0x1C:
                signal_send_pgrp(pty->pgid, SIGQUIT);
                break;
            default:
                if (fifo_enqueue(pty->ififo, buf[i]) < 0)
                    goto done;
                continue;
        }

        if (fifo_enqueue(pty->ofifo, buf[i]) < 0)
            break;
    }

done:
    if (i > 0)
        vfs_wake_waiters(pty->slave);
    return i;
}

long master_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;
    if (fifo_is_empty(pty->ofifo))
        return -EAGAIN;

    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (fifo_dequeue(pty->ofifo, &buf[i]) < 0)
            break;
    }
    
    if (i > 0)
        vfs_wake_waiters(pty->slave);
    return i;
}

long master_poll(vfs_node_t *node, long events) {
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;

    long revents = 0;
    if (events & POLLIN && !fifo_is_empty(pty->ofifo))
        revents |= POLLIN;
    if (events & POLLOUT && !fifo_is_full(pty->ififo))
        revents |= POLLOUT;
    return revents;
}

long ptmx_ioctl(struct vfs_node *node, int op, void *arg) {
    pty_t *pty = node->device;
    if (!pty)
        return -EINVAL;

    switch (op) {
        case TCGETS:
            return copy_to_user(arg, &pty->tio, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:
            return copy_from_user(&pty->tio, arg, sizeof(struct termios));
        case TIOCGWINSZ:
            return copy_to_user(arg, &pty->ws, sizeof(struct winsize));
        case TIOCSWINSZ:
            return copy_from_user(&pty->ws, arg, sizeof(struct winsize));
        case TIOCGPGRP:
            return copy_to_user(arg, &pty->pgid, sizeof pty->pgid);
        case TIOCSPGRP:
            return copy_from_user(&pty->pgid, arg, sizeof pty->pgid);
        case TIOCGPTN:
            return copy_to_user(arg, &pty->id, sizeof(int));
        case TIOCSPTLCK:
            return copy_from_user(&pty->locked, arg, sizeof(int));
        case TCXONC:
            // TODO
            return 0;
        case TIOCSCTTY:
            if (this_proc->pid != this_proc->sid)
                return -EPERM;

            if (pty->pgid == 0)
                pty->pgid = this_proc->pgid;
            return 0;
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
}

vfs_ops_t slave_ops = {
    .open = slave_open,
    .close = slave_close,
    .read = slave_read,
    .write = slave_write,
    .poll = slave_poll,
    .ioctl = ptmx_ioctl
};

vfs_ops_t master_ops = {
    .close = master_close,
    .read = master_read,
    .write = master_write,
    .poll = master_poll,
    .ioctl = ptmx_ioctl
};

vfs_ops_t ptmx_ops = {
    .open = ptmx_open,
    .ioctl = ptmx_ioctl
};

vfs_node_t *ptmx_open(vfs_node_t *node, int flags) {
    (void)node;
    (void)flags;
    pty_t *pty = pty_create();
    if (!pty)
        return NULL;
    
    vfs_node_t *slave = devfs_create_numbered(DEVFS_PTY);
    slave->ops = &slave_ops;
    slave->device = pty;
    slave->inode = 100000;
    pty->slave = slave;

    vfs_node_t *master = vfs_create_node("[pty]", VFS_PTY);
    master->ops = &master_ops;
    master->device = pty;
    master->inode = 100000;
    pty->master = master;

    return master;
}

void pty_initialize(void) {
    pty_bitmap = kmalloc(PTY_BITMAP_SIZE);
    memset(pty_bitmap, 0, PTY_BITMAP_SIZE);
    devfs_create_node("pts", VFS_DIRECTORY);

    vfs_node_t *ptmx = devfs_create_node("ptmx", VFS_CHARDEVICE);
    ptmx->ops = &ptmx_ops;
}