#include <kernel/errno.h>
#include <fcntl.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/sched.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/unixpipe.h>

struct fd fd_new(struct vfs_node *node, int flags) {
    struct fd fd;
    fd.node = node;
    fd.flags = flags;
    fd.offset = 0;
    fd.open = true;

    fd.tio.c_iflag = BRKINT | ICRNL | IXON;
    fd.tio.c_oflag = OPOST | ONLCR;
    fd.tio.c_cflag = CS8 | CREAD;
    fd.tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    fd.tio.c_cc[VINTR] = 3;
    fd.tio.c_cc[VQUIT] = 28;
    fd.tio.c_cc[VERASE] = 127;
    fd.tio.c_cc[VKILL] = 21;
    fd.tio.c_cc[VEOF] = 4;
    fd.tio.c_cc[VTIME] = 0;
    fd.tio.c_cc[VMIN] = 1;
    fd.tio.c_cc[VSTART] = 17;
    fd.tio.c_cc[VSTOP] = 19;
    fd.tio.c_cc[VSUSP] = 26;
    return fd;
}

int fd_create(struct vfs_node *node, int flags) {
    for (size_t i = 0; i < sizeof this->fd_table / sizeof(struct fd); i++) {
        if (!this->fd_table[i].node && !this->fd_table[i].open) {
            this->fd_table[i] = fd_new(node, flags);
            if (flags & O_APPEND) {
                this->fd_table[i].offset = node->size - 1;
            }
            return i;
        }
    }
    return -1;
}

int fd_open(const char *path, int flags) {
    struct vfs_node *node = vfs_open(this->cwd, path, (flags & O_CREAT) ? true : false, false);
    if (!node) return -ENOENT;

    int fd = fd_create(node, flags);
    if (fd < 0) {
        vfs_close(node);
        return -EMFILE;
    }
    return fd;
}

int fd_close(int fd) {
    if (fd < 0 || fd > (signed)(sizeof this->fd_table / sizeof(struct fd))) {
        return -EBADF;
    }

    struct fd *file = &this->fd_table[fd];
    if (!file->node)
        return -EBADF;
    vfs_close(file->node);
    memset(file, 0, sizeof(struct fd));
    return 0;
}

int fd_dup(int oldfd_num, int newfd_num) {
    if (oldfd_num == newfd_num)
        return -EINVAL;
    if (newfd_num > USER_MAX_FDS)
        return -EBADF;

    struct fd *oldfd = &this->fd_table[oldfd_num];
    struct fd *newfd = &this->fd_table[newfd_num];

    if (!oldfd->node)
        return -EBADF;
    if (newfd->node)
        fd_close(newfd_num);
    if (oldfd->node->type == VFS_UNIXPIPE) {
        struct unix_pipe *pipe = oldfd->node->device;
        if (!strcmp(oldfd->node->name, "[pipe::read]"))
            pipe->read_refs++;
        else if (!strcmp(oldfd->node->name, "[pipe::write]"))
            pipe->write_refs++;
    }
    memcpy(newfd, oldfd, sizeof(struct fd));
    return newfd_num;
}

struct fd *fd_get(int fd) {
    if (fd < 0 || fd >= USER_MAX_FDS)
        return NULL;
    if (!this->fd_table[fd].node)
        return NULL;
    return &this->fd_table[fd];
}