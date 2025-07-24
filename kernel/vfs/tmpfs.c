#include <stddef.h>
#include <errno.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

typedef struct tmpfs_file {
    void *data;
} tmpfs_file_t;

struct vfs_node *tmpfs_create(struct vfs_node *parent, const char *name);
struct vfs_node *tmpfs_mkdir(struct vfs_node *parent, const char *name);
long tmpfs_remove(struct vfs_node *node);
long tmpfs_rmdir(struct vfs_node *node);

struct vfs_driver_ops tmpfs_driver = {
    .create = tmpfs_create,
    .remove = tmpfs_remove,
    .mkdir = tmpfs_mkdir,
    .rmdir = tmpfs_rmdir
};

long tmpfs_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    tmpfs_file_t *file = node->device;
    if (!file->data)
        return 0;
    size_t count = len < node->size - offset ? len : node->size - offset;
    memcpy(buffer, file->data + offset, count);
    return count;
}

long tmpfs_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    tmpfs_file_t *file = node->device;
    if (offset == -1) {
        offset = node->size;
    }
    if (!file->data) {
        file->data = kmalloc(offset + len);
        node->size = offset + len;
    } else if (offset + len > node->size) {
        file->data = krealloc(file->data, offset + len);
        node->size = offset + len;
    } else if (offset + len < node->size) {
        node->size = offset + len;
    }
    memcpy((char *)file->data + offset, buffer, len);
    return len;
}

struct vfs_node *tmpfs_create(struct vfs_node *parent, const char *name) {
    struct vfs_node *node = vfs_create_node(name, VFS_FILE);
    tmpfs_file_t *file = kmalloc(sizeof(tmpfs_file_t));
    file->data = NULL;
    node->size = 0;
    node->driver = tmpfs_driver;
    node->device = file;
    node->read = tmpfs_read;
    node->write = tmpfs_write;
    vfs_add_node(parent, node);
    return node;
}

long tmpfs_remove(struct vfs_node *node) {
    if (!node || node->type != VFS_FILE)
        return -EINVAL;
    tmpfs_file_t *file = node->device;
    if (file->data)
        kfree(file->data);
    kfree(file);
    return 0;
}

struct vfs_node *tmpfs_mkdir(struct vfs_node *parent, const char *name) {
    struct vfs_node *dir = vfs_create_node(name, VFS_DIRECTORY);
    if (dir) {
        dir->driver = parent->driver;
        vfs_add_node(parent, dir);
    }
    return dir;
}

long tmpfs_rmdir(struct vfs_node *node) {
    return 0;
}

void tmpfs_initialize(void) {
    struct vfs_node *tmp = vfs_create_node("tmp", VFS_DIRECTORY);
    tmp->driver = tmpfs_driver;
    vfs_add_node(NULL, tmp);
}