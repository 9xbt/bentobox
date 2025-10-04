#include <kernel/unixpipe.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/vfs.h>

struct file file_new(vfs_node_t *node, int flags) {
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

static int fd_table_expand(int min_size) {
    int new_size = min_size + 1;
    struct file *new_files = krealloc(this_proc->files, new_size * sizeof(struct file));
    if (!new_files) return -ENOMEM;
    
    memset(&new_files[this_proc->max_files], 0, (new_size - this_proc->max_files) * sizeof(struct file));
    this_proc->files = new_files;
    this_proc->max_files = new_size;
    return 0;
}

int file_create(vfs_node_t *node, int flags) {
    if (!node)
        return -ENOENT;
    int i;
    for (i = 0; i < this_proc->max_files; i++) {
        if (!this_proc->files[i].open) {
            this_proc->files[i] = file_new(node, flags);
            if (flags & O_APPEND)
                this_proc->files[i].offset = node->size;
            return i;
        }
    }

    if (fd_table_expand(this_proc->max_files) < 0)
        return -EMFILE;
    this_proc->files[i] = file_new(node, flags);
    if (flags & O_APPEND)
        this_proc->files[i].offset = node->size;
    return -EMFILE;
}

int file_open(vfs_node_t *cwd, const char *path, int flags) {
    vfs_node_t *node = vfs_open(cwd, path, flags);
    if (!node)
        return -ENOENT;

    int file = file_create(node, flags);
    if (file < 0) {
        vfs_close(node);
        return file;
    }
    return file;
}

int file_close(int fd) {
    if (fd < 0 || fd >= this_proc->max_files)
        return -EBADF;

    struct file *file = &this_proc->files[fd];
    if (!file->open)
        return -EBADF;
    vfs_close(file->node);
    memset(file, 0, sizeof(struct file));
    return 0;
}

struct file *file_get(int file) {
    if (file < 0 || file >= this_proc->max_files)
        return NULL;
    if (!this_proc->files[file].node)
        return NULL;
    return &this_proc->files[file];
}

struct file *file_get_from_node(vfs_node_t *node) {
    for (int i = 0; i < this_proc->max_files; i++) {
        if (this_proc->files[i].open && this_proc->files[i].node == node) {
            return &this_proc->files[i];
        }
    }
    return NULL;
}

int file_dup(int oldfd, int newfd, int flags) {
    struct file *oldfile = file_get(oldfd);
    if (!oldfile)
        return -EBADF;
    
    if (newfd == -1) {
        for (int i = 0; i < this_proc->max_files; i++) {
            if (!this_proc->files[i].open) {
                newfd = i;
                break;
            }
        }
        if (newfd == -1) {
            if (fd_table_expand(this_proc->max_files) < 0)
                return -EMFILE;
            newfd = this_proc->max_files - 1;
        }
    } else {
        if (oldfd == newfd)
            return newfd;
        if (newfd < 0)
            return -EBADF;
        if (newfd >= this_proc->max_files && fd_table_expand(newfd) < 0)
            return -EMFILE;
        if (this_proc->files[newfd].open)
            file_close(newfd);
    }
    
    this_proc->files[newfd] = *oldfile;
    this_proc->files[newfd].flags = flags;

    if (oldfile->node->type == VFS_UNIXPIPE) {
        struct unix_pipe *pipe = oldfile->node->device;
        if (!strcmp(oldfile->node->name, "[pipe::read]"))
            pipe->read_refs++;
        else if (!strcmp(oldfile->node->name, "[pipe::write]"))
            pipe->write_refs++;
    }
    return newfd;
}