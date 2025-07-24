#include <kernel/vfs.h>

struct vfs_node *devfs_create(struct vfs_node *parent, const char *name);
struct vfs_node *devfs_mkdir(struct vfs_node *parent, const char *name);
long devfs_remove(struct vfs_node *node);
long devfs_rmdir(struct vfs_node *node);

struct vfs_driver_ops devfs_driver = {
    .create = devfs_create,
    .remove = devfs_remove,
    .mkdir = devfs_mkdir,
    .rmdir = devfs_rmdir
};

struct vfs_node *devfs_create(struct vfs_node *parent, const char *name) {
    struct vfs_node *node = vfs_create_node(name, VFS_FILE);
    node->size = 0;
    node->driver = devfs_driver;
    vfs_add_node(parent, node);
    return node;
}

long devfs_remove(struct vfs_node *node) {
    return 0;
}

struct vfs_node *devfs_mkdir(struct vfs_node *parent, const char *name) {
    struct vfs_node *dir = vfs_create_node(name, VFS_DIRECTORY);
    if (dir) {
        dir->driver = parent->driver;
        vfs_add_node(parent, dir);
    }
    return dir;
}

long devfs_rmdir(struct vfs_node *node) {
    return 0;
}

void devfs_initialize(void) {
    vfs_devfs = vfs_create_node("dev", VFS_DIRECTORY);
    vfs_devfs->driver = devfs_driver;
    vfs_add_node(vfs_root, vfs_devfs);
}