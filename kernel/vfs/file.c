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
    return i;
}

int file_open(vfs_node_t *cwd, const char *path, int flags, unsigned int mode) {
    vfs_node_t *node = vfs_open(cwd, path, 0);
    if (!node && flags & O_CREAT) {
        node = vfs_open(cwd, path, flags);
        if (node)
            node->perms = mode & ~this_proc->umask;
    }
    if (!node)
        return -ENOENT;
    if (flags & O_DIRECTORY && node->type != VFS_DIRECTORY)
        return -ENOTDIR;

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
    if (!this_proc->files[file].node || !this_proc->files[file].open)
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

int file_dup(int oldfd, int newfd, int flags, bool exact_fd) {
    if (!file_get(oldfd))
        return -EBADF;
    struct file oldfile = *file_get(oldfd);
    
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
    } else if (exact_fd) {
        if (oldfd == newfd)
            return newfd;
        if (newfd < 0)
            return -EBADF;
        if (newfd >= this_proc->max_files && fd_table_expand(newfd) < 0)
            return -EMFILE;
        if (this_proc->files[newfd].open)
            file_close(newfd);
    } else {
        int min_fd = newfd;
        newfd = -1;
        for (int i = min_fd; i < this_proc->max_files; i++) {
            if (!this_proc->files[i].open) {
                newfd = i;
                break;
            }
        }
        if (newfd == -1) {
            int expand_to = (min_fd >= this_proc->max_files) ? min_fd : this_proc->max_files;
            if (fd_table_expand(expand_to) < 0)
                return -EMFILE;
            newfd = this_proc->max_files - 1;
        }
    }
    
    this_proc->files[newfd] = oldfile;
    this_proc->files[newfd].flags = flags;

    if (oldfile.node->type == VFS_UNIXPIPE) {
        struct unix_pipe *pipe = oldfile.node->device;
        if (!strcmp(oldfile.node->name, "[pipe::read]"))
            pipe->read_refs++;
        else if (!strcmp(oldfile.node->name, "[pipe::write]"))
            pipe->write_refs++;
    }
    return newfd;
}