#include <kernel/printf.h>
#include <kernel/list.h>
#include <kernel/vfs.h>

vfs_node_t *dev = NULL;

vfs_node_t *devfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);

vfs_ops_t devfs_ops = {
    .create = devfs_create
};

vfs_node_t *devfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    vfs_node_t *node = vfs_create_node(name, type);
    node->size = 0;
    vfs_add_node(parent, node);
    return node;
}

vfs_node_t *devfs_create_node(const char *name, vfs_node_type_t type) {
    return devfs_create(dev, name, type);
}

vfs_node_t *devfs_create_event(void) {
    static int i = 0;
    char name[MAX_PATH];
    snprintf(name, sizeof name, "event%d", i++);
    return devfs_create(dev, name, VFS_CHARDEVICE);
}

vfs_node_t *devfs_create_disk(void) {
    static int i = 0;
    char name[MAX_PATH];
    snprintf(name, sizeof name, "sd%c", 'a' + i++);
    return devfs_create(dev, name, VFS_CHARDEVICE);
}

long devfs_mount(vfs_node_t *node, vfs_node_t *device, long flags) {
    (void)device;
    (void)flags;

    foreach(i, dev->children) {
        list_insert(node->children, i->value);
    }

    return 0;
}

vfs_mount_ops_t devfs_mount_ops = {
    .type  = "dev",
    .nodev = true,
    .mount = devfs_mount
};

void devfs_initialize(void) {
    vfs_register(&devfs_mount_ops);

    dev = vfs_create_node("dev", VFS_DIRECTORY);
    dev->ops = &devfs_ops;
    vfs_add_node(NULL, dev);
}