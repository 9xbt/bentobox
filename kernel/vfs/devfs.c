#include <kernel/vfs.h>

struct vfs_node *devfs_create(struct vfs_node *parent, const char *name, vfs_node_type_t type);

vfs_ops_t devfs_ops = {
    .create = devfs_create
};

struct vfs_node *devfs_create(struct vfs_node *parent, const char *name, vfs_node_type_t type) {
    struct vfs_node *node = vfs_create_node(name, type);
    node->size = 0;
    node->ops = &devfs_ops;
    vfs_add_node(parent, node);
    return node;
}

void devfs_initialize(void) {
    vfs_node_t *dev = vfs_create_node("dev", VFS_DIRECTORY);
    dev->ops = &devfs_ops;
    vfs_add_node(NULL, dev);
}