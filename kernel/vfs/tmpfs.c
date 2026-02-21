#include <stddef.h>
#include <kernel/errno.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

typedef struct tmpfs {
    void *data;
    int refcount;
} tmpfs_t;

vfs_node_t *tmpfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);
long tmpfs_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long tmpfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len);
long tmpfs_remove(vfs_node_t *node);
long tmpfs_rename(vfs_node_t *node, vfs_node_t *parent, const char *name);
long tmpfs_chmod(vfs_node_t *node, unsigned int mode);
long tmpfs_link(vfs_node_t *old_node, vfs_node_t *new_node);

vfs_ops_t tmpfs_ops = {
    .read   = tmpfs_read,
    .write  = tmpfs_write,
    .create = tmpfs_create,
    .remove = tmpfs_remove,
    .rename = tmpfs_rename,
    .chmod  = tmpfs_chmod,
    .link   = tmpfs_link
};

long tmpfs_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    tmpfs_t *file = node->device;
    if (!file->data || (size_t)offset >= node->size)
        return 0;
    size_t count = len < node->size - offset ? len : node->size - offset;
    memcpy(buffer, file->data + offset, count);
    return count;
}

long tmpfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    tmpfs_t *file = node->device;
    if (offset == -1)
        offset = node->size;
    size_t size = offset + len;
    
    if (!file->data) {
        file->data = kmalloc(size);
        memset(file->data, 0, size);
        node->size = size;
    } else if (size > node->size) {
        void *new_data = krealloc(file->data, size);
        if ((size_t)offset > node->size)
            memset((char *)new_data + node->size, 0, offset - node->size);
        file->data = new_data;
        node->size = size;
    }

    memcpy((char *)file->data + offset, buffer, len);
    return len;
}

vfs_node_t *tmpfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    vfs_node_t *node = vfs_create_node(name, type);
    if (!node)
        return NULL;
    if (type == VFS_DIRECTORY) {
        node->ops = &tmpfs_ops;
    } else if (type == VFS_FILE) {
        tmpfs_t *file = kmalloc(sizeof(tmpfs_t));
        file->data = NULL;
        file->refcount = 1;
        node->size = 0;
        node->ops = &tmpfs_ops;
        node->device = file;
    }
    vfs_add_node(parent, node);
    return node;
}

long tmpfs_remove(vfs_node_t *node) {
    if (node->device) {
        tmpfs_t *file = node->device;
        if (--file->refcount <= 0) {
            if (file->data)
                kfree(file->data);
            kfree(file);
        }
    }
    return 0;
}

long tmpfs_chmod(vfs_node_t *node, unsigned int mode) {
    (void)node;
    (void)mode;
    return 0;
}

long tmpfs_rename(vfs_node_t *node, vfs_node_t *parent, const char *name) {
    (void)node;
    (void)name;
    (void)parent;
    return 0;
}

long tmpfs_link(vfs_node_t *old_node, vfs_node_t *new_node) {
    tmpfs_t *file = old_node->device;
    new_node->device = file;
    file->refcount++;
    return 0;
}

long tmpfs_mount(vfs_node_t *node, vfs_node_t *device, long flags) {
    (void)node;
    (void)device;
    (void)flags;
    node->ops = &tmpfs_ops;
    return 0;
}

void tmpfs_unmount_recursive(vfs_node_t *node) {
    foreach_safe(i, node->children) {
        vfs_node_t *child = i->value;
        if (child->type == VFS_DIRECTORY)
            tmpfs_unmount_recursive(child);
        if (child->device == node->device)
            vfs_remove_node(child);
    }
}

long tmpfs_unmount(vfs_node_t *node, long flags) {
    (void)flags;
    tmpfs_unmount_recursive(node);
    node->ops = node->parent->ops;
    return 0;
}

vfs_mount_ops_t tmpfs_mount_ops = {
    .type  = "tmp",
    .nodev = true,
    .mount = tmpfs_mount,
    .unmount = tmpfs_unmount
};

void tmpfs_initialize(void) {
    vfs_register(&tmpfs_mount_ops);
}