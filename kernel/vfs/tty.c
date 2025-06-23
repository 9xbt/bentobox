#include <kernel/errno.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/vfs.h>
#include <kernel/sched.h>

long tty_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    return vfs_read(this->fd_table[0].node, buffer, offset, len);
}

long tty_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    return vfs_write(this->fd_table[1].node, buffer, offset, len);
}

long tty_ioctl(int fd, int op, void *arg) {
    /* TODO: make this properly call the right ioctl */
    long ret = this->fd_table[1].node->ioctl(fd, op, arg);
    return ret == -EINVAL ? this->fd_table[0].node->ioctl(fd, op, arg) : ret;
}

void tty_initialize(void) {
    vfs_node_t *tty = vfs_create_node("tty", VFS_CHARDEVICE);
    tty->read = tty_read;
    tty->write = tty_write;
    tty->isatty = true;
    tty->ioctl = tty_ioctl;
    vfs_add_device(tty);
}