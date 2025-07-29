#include <kernel/vfs.h>

struct vfs_node *devfs_create(struct vfs_node *parent, const char *name);
struct vfs_node *devfs_mkdir(struct vfs_node *parent, const char *name);

struct vfs_node *devfs_create(struct vfs_node *parent, const char *name) {
    struct vfs_node *node = vfs_create_node(name, VFS_FILE);
    node->size = 0;
    node->create = devfs_create;
    node->mkdir = devfs_mkdir;
    vfs_add_node(parent, node);
    return node;
}

struct vfs_node *devfs_mkdir(struct vfs_node *parent, const char *name) {
    struct vfs_node *dir = vfs_create_node(name, VFS_DIRECTORY);
    if (dir) {
        dir->create = devfs_create;
        dir->mkdir = devfs_mkdir;
        vfs_add_node(parent, dir);
    }
    return dir;
}

void devfs_initialize(void) {
    vfs_devfs = vfs_create_node("dev", VFS_DIRECTORY);
    vfs_devfs->create = devfs_create;
    vfs_devfs->mkdir = devfs_mkdir;
    vfs_add_node(vfs_root, vfs_devfs);
}