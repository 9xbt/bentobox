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
    if (type == VFS_DIRECTORY)
        node->ops = &devfs_ops;
    return vfs_add_node(parent, node);
}

vfs_node_t *devfs_create_node(const char *name, vfs_node_type_t type) {
    return devfs_create(dev, name, type);
}

vfs_node_t *devfs_create_numbered(devfs_node_type_t type) {
    static int i[16] = { [DEVFS_SSD] = 'a', [DEVFS_TTY] = 1 };
    char name[MAX_PATH];
    char *fmt = NULL;
    vfs_node_t *parent = dev;

    switch (type) {
        case DEVFS_EVENT:
            fmt = "event%d";
            break;
        case DEVFS_FB:
            fmt = "fb%d";
            break;
        case DEVFS_SSD:
            fmt = "sd%c";
            break;
        case DEVFS_PTY:
            fmt = "%d";
            parent = vfs_lookup(NULL, "/dev/pts", true, VFS_DIRECTORY);
            break;
        case DEVFS_TTY:
            fmt = "tty%d";
            break;
        case DEVFS_STTY:
            fmt = "ttyS%d";
            break;
    }
    
    snprintf(name, sizeof name, fmt, i[type]++);
    return devfs_create(parent, name, type == DEVFS_SSD ? VFS_BLOCKDEVICE : VFS_CHARDEVICE);
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