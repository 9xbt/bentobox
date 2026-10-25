#include <stddef.h>
#include <kernel/spinlock.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

typedef struct tmpfs {
    spinlock_t lock;
    void *data;
    int refcount;
    size_t size;
    size_t capacity;
} tmpfs_t;

vfs_result_t tmpfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);
long tmpfs_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long tmpfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len);
long tmpfs_remove(vfs_node_t *node);
long tmpfs_rename(vfs_node_t *node, vfs_node_t *parent, const char *name);
long tmpfs_chmod(vfs_node_t *node, unsigned int mode);
long tmpfs_link(vfs_node_t *old_node, vfs_node_t *new_node);
long tmpfs_mount(vfs_node_t *node, vfs_node_t *device, long flags);
long tmpfs_unmount(vfs_node_t *node, long flags);
long tmpfs_truncate(vfs_node_t *node, size_t length);

vfs_ops_t tmpfs_ops = {
    .read     = tmpfs_read,
    .write    = tmpfs_write,
    .create   = tmpfs_create,
    .remove   = tmpfs_remove,
    .rename   = tmpfs_rename,
    .chmod    = tmpfs_chmod,
    .link     = tmpfs_link,
    .truncate = tmpfs_truncate
};

vfs_mount_ops_t tmpfs_mount_ops = {
    .type     = "tmp",
    .nodev    = true,
    .mount    = tmpfs_mount,
    .unmount  = tmpfs_unmount
};

long tmpfs_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    tmpfs_t *file = node->device;
    if (!file)
        return -EINVAL;
    
    acquire(&file->lock);
    if (!len || !file->data || (size_t)offset >= file->size) {
        release(&file->lock);
        return 0;
    }

    size_t count = len < file->size - offset ? len : file->size - offset;
    memcpy(buffer, file->data + offset, count);
    release(&file->lock);
    return count;
}

long tmpfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    tmpfs_t *file = node->device;
    if (!file)
        return -EINVAL;
    if (!len)
        return 0;

    acquire(&file->lock);
    if (offset == -1)
        offset = file->size;
    if (offset < 0 || (size_t)offset + len < len) {
        release(&file->lock);
        return -EINVAL;
    }

    size_t size = offset + len;
    if (size > file->capacity) {
        size_t cap = file->capacity ? file->capacity : PAGE_SIZE;
        while (cap < size)
            cap *= 2;

        file->data = file->data ? krealloc(file->data, cap) : kmalloc(cap);
        file->capacity = cap;
    }
    if ((size_t)offset > file->size)
        memset(file->data + file->size, 0, offset - file->size);

    memcpy(file->data + offset, buffer, len);
    if (size > file->size)
        file->size = size;
    node->size = file->size;

    release(&file->lock);
    return len;
}

vfs_result_t tmpfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    vfs_node_t *node = vfs_create_node(name, type);
    static uint64_t inode = 1;
    if (type == VFS_DIRECTORY) {
        node->ops = &tmpfs_ops;
        node->inode = inode++;
    } else if (type == VFS_FILE) {
        tmpfs_t *file = kmalloc(sizeof(tmpfs_t));
        file->lock = 0;
        file->data = NULL;
        file->refcount = 1;
        file->size = 0;
        file->capacity = 0;
        node->inode = inode++;
        node->ops = &tmpfs_ops;
        node->device = file;
    }
    vfs_add_node(parent, node);
    return (vfs_result_t){ node, 0 };
}

long tmpfs_remove(vfs_node_t *node) {
    tmpfs_t *file = node->device;
    if (!file)
        return 0;

    if (--file->refcount <= 0) {
        if (file->data)
            kfree(file->data);
        kfree(file);
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
    if (!file)
        return -EINVAL;

    new_node->device = file;
    file->refcount++;
    return 0;
}

long tmpfs_truncate(vfs_node_t *node, size_t length) {
    tmpfs_t *file = node->device;
    if (!file)
        return -EINVAL;
    if (!file->size)
        return 0;

    acquire(&file->lock);
    if (!length) {
        kfree(file->data);
        file->data = NULL;
        file->capacity = 0;
    } else if (length > file->capacity) {
        size_t cap = file->capacity ? file->capacity : PAGE_SIZE;
        while (cap < length)
            cap *= 2;
        file->data = file->data ? krealloc(file->data, cap) : kmalloc(cap);
        file->capacity = cap;
    }

    if (length > file->size)
        memset(file->data + file->size, 0, length - file->size);
    file->size = length;

    release(&file->lock);
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

void tmpfs_initialize(void) {
    vfs_register(&tmpfs_mount_ops);
}