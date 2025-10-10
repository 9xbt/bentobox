#include <kernel/vfs.h>

vfs_node_t *devfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);

vfs_ops_t devfs_ops = {
    .create = devfs_create
};

vfs_node_t *devfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    vfs_node_t *node = vfs_create_node(name, type);
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