#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/list.h>
#include <kernel/vfs.h>

vfs_node_t *dev = NULL;
uint8_t *devfs_bitmap[DEVFS_MAX];
spinlock_t devfs_bitmap_lock = 0;

vfs_result_t devfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);

vfs_ops_t devfs_ops = {
    .create = devfs_create
};

vfs_result_t devfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    vfs_node_t *node = vfs_create_node(name, type);
    if (type == VFS_DIRECTORY)
        node->ops = &devfs_ops;
    return (vfs_result_t){ vfs_add_node(parent, node), 0 };
}

vfs_node_t *devfs_create_node(const char *name, vfs_node_type_t type) {
    return devfs_create(dev, name, type).node;
}

static int devfs_allocate_id(devfs_type_t type) {
    acquire(&devfs_bitmap_lock);
    for (int id = 0; id < DEVFS_BITMAP_SIZE * 8; id++) {
        if (!bitmap_get(devfs_bitmap[type], id)) {
            bitmap_set(devfs_bitmap[type], id);
            release(&devfs_bitmap_lock);
            return id;
        }
    }
    release(&devfs_bitmap_lock);
    return -1;
}

static void devfs_free_id(devfs_type_t type, int id) {
    acquire(&devfs_bitmap_lock);
    bitmap_clear(devfs_bitmap[type], id);
    release(&devfs_bitmap_lock);
}

vfs_node_t *devfs_create_numbered(devfs_type_t type) {
    char name[MAX_PATH];
    char *fmt = NULL;
    vfs_node_t *parent = dev;
    vfs_node_type_t node_type = VFS_CHARDEVICE;

    int id = devfs_allocate_id(type);
    switch (type) {
        case DEVFS_EVENT:
            fmt = "event%d";
            parent = vfs_lookup(NULL, "/dev/input", true, VFS_DIRECTORY).node;
            assert(parent);
            break;
        case DEVFS_FB:
            fmt = "fb%d";
            break;
        case DEVFS_SSD:
            fmt = "sd%c";
            id += 'a';
            node_type = VFS_BLOCKDEVICE;
            break;
        case DEVFS_PTY:
            fmt = "%d";
            parent = vfs_lookup(NULL, "/dev/pts", true, VFS_DIRECTORY).node;
            assert(parent);
            break;
        case DEVFS_TTY:
            fmt = "tty%d";
            break;
        case DEVFS_STTY:
            fmt = "ttyS%d";
            break;
        default:
            break;
    }
    
    snprintf(name, sizeof name, fmt, id);
    vfs_result_t r = devfs_create(parent, name, node_type);
    r.node->inode = DEVFS_INODE_BASE + id;
    return r.node;
}

void devfs_remove_numbered(devfs_type_t type, vfs_node_t *node) {
    assert(node->inode >= DEVFS_INODE_BASE);
    devfs_free_id(type, node->inode - DEVFS_INODE_BASE);
    vfs_remove_node(node);
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

    for (int i = 0; i < DEVFS_MAX; i++) {
        devfs_bitmap[i] = kmalloc(DEVFS_BITMAP_SIZE);
        memset(devfs_bitmap[i], 0, DEVFS_BITMAP_SIZE);
    }
    bitmap_set(devfs_bitmap[DEVFS_TTY], 0);
}