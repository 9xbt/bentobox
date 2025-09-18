#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/vfs.h>

struct file file_new(struct vfs_node *node, int flags) {
    struct file file;
    file.node = node;
    file.flags = flags;
    file.offset = 0;
    file.open = true;

    file.tio.c_iflag = BRKINT | ICRNL | IXON;
    file.tio.c_oflag = OPOST | ONLCR;
    file.tio.c_cflag = CS8 | CREAD;
    file.tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    file.tio.c_cc[VINTR] = 3;
    file.tio.c_cc[VQUIT] = 28;
    file.tio.c_cc[VERASE] = 127;
    file.tio.c_cc[VKILL] = 21;
    file.tio.c_cc[VEOF] = 4;
    file.tio.c_cc[VTIME] = 0;
    file.tio.c_cc[VMIN] = 1;
    file.tio.c_cc[VSTART] = 17;
    file.tio.c_cc[VSTOP] = 19;
    file.tio.c_cc[VSUSP] = 26;
    return file;
}

int file_create(struct vfs_node *node, int flags) {
    if (!node)
        return -ENOENT;
    int i;
    for (i = 0; i < this_proc->max_files; i++) {
        if (!this_proc->files[i].node && !this_proc->files[i].open) {
            this_proc->files[i] = file_new(node, flags);
            if (flags & O_APPEND) {
                this_proc->files[i].offset = node->size;
            }
            return i;
        }
    }
    this_proc->files = krealloc(this_proc->files, ++this_proc->max_files * sizeof(struct file));
    this_proc->files[i] = file_new(node, flags);
    if (flags & O_APPEND) {
        this_proc->files[i].offset = node->size;
    }
    return -EMFILE;
}

int file_open(const char *path, int flags) {
    struct vfs_node *node = vfs_open(NULL /*this->cwd*/, path, flags);
    if (!node) return -ENOENT;

    int file = file_create(node, flags);
    if (file < 0) {
        vfs_close(node);
        return file;
    }
    return file;
}

int file_close(int fd) {
    if (fd < 0 || fd >= this_proc->max_files) {
        return -EBADF;
    }

    struct file *file = &this_proc->files[fd];
    if (!file->node)
        return -EBADF;
    vfs_close(file->node);
    memset(file, 0, sizeof(struct file));
    return 0;
}

int file_dup(int oldfd_num, int newfd_num) {
    if (oldfd_num == newfd_num)
        return -EINVAL;
    if (newfd_num >= this_proc->max_files) {
        this_proc->files = krealloc(this_proc->files, ++this_proc->max_files * sizeof(struct file));
    }

    struct file *oldfd = &this_proc->files[oldfd_num];
    struct file *newfd = &this_proc->files[newfd_num];

    if (!oldfd->node)
        return -EBADF;
    if (newfd->node)
        file_close(newfd_num);
    memcpy(newfd, oldfd, sizeof(struct file));
    return newfd_num;
}

struct file *file_get(int file) {
    if (file < 0 || file >= this_proc->max_files)
        return NULL;
    if (!this_proc->files[file].node)
        return NULL;
    return &this_proc->files[file];
}

struct file *file_get_from_node(struct vfs_node *node) {
    for (int i = 0; i < this_proc->max_files; i++) {
        if (this_proc->files[i].open && this_proc->files[i].node == node) {
            return &this_proc->files[i];
        }
    }
    return NULL;
}