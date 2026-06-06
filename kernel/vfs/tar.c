#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/tar.h>
#include <kernel/vfs.h>
#include <limine.h>

vfs_result_t tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type);
long tar_remove(vfs_node_t *node);
long tar_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long tar_mount(vfs_node_t *node, vfs_node_t *device, long flags);

vfs_ops_t tar_ops = {
    .create = tar_create,
    .remove = tar_remove,
    .read   = tar_read
};

vfs_mount_ops_t tar_mount_ops = {
    .type     = "tar",
    .nodev    = true,
    .readonly = true,
    .mount    = tar_mount
};

vfs_result_t tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    vfs_node_t *node = vfs_create_node(name, type);
    node->ops = &tar_ops;
    return (vfs_result_t){ vfs_add_node(parent, node), 0 };
}

long tar_remove(vfs_node_t *node) {
    return node->device ? -EROFS : 0;
}

long tar_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    struct tar *tar = (struct tar *)node->device;
    if (!tar)
        return -EINVAL;

    if (memcmp(tar->ustar, "ustar", 5)) {
        dprintf(LOG_ERR, "\033[93mtar:\033[0m invalid signature at 0x%p\n", tar);
        return -EINVAL;
    }
    
    size_t count = len < node->size - offset ? len : node->size - offset;
    memcpy(buffer, node->device + 512 + offset, count);
    return count;
}

int oct2bin(char *oct, int size) {
    unsigned int out = 0;
    int i = 0;
    while ((i < size) && oct[i]){
        out = (out << 3) | (unsigned int) (oct[i++] - '0');
    }
    return out;
}

long tar_mount(vfs_node_t *node, vfs_node_t *device, long flags) {
    (void)flags;
    node->ops = &tar_ops;

    struct tar *tar = (struct tar *)device;
    uint64_t inode = 1;

    char *override = NULL;
    while (!memcmp(tar->ustar, "ustar", 5)) {
        int filesize = oct2bin(tar->size, sizeof(tar->size));
        int mode = oct2bin(tar->mode + 4, 3);

        vfs_node_type_t type;
        switch (tar->type) {
            case '5':
                type = VFS_DIRECTORY;
                break;
            case '0':
            case '\0':
                type = VFS_FILE;
                break;
            case '2':
                type = VFS_SYMLINK;
                break;
            case 'L':
                override = (char *)tar + 512;
                goto skip;
            default:
                dprintf(LOG_WARNING, "\033[93mtar:\033[0m skipping '%s': unsupported type %c\n", tar->name, tar->type);
                goto skip;
        }

        vfs_result_t r = vfs_lookup(node, override ?: tar->name, true, type);
        override = NULL;
        if (!r.node) {
            dprintf(LOG_WARNING, "\033[93mtar:\033[0m failed to create %s: %s\n", tar->name, strerror(r.error));
        } else if (type == VFS_FILE) {
            r.node->device = tar;
            r.node->size = filesize;
            r.node->perms = mode;
            r.node->inode = inode++;
        } else if (type == VFS_DIRECTORY) {
            r.node->size = filesize;
            r.node->perms = mode;
            r.node->inode = inode++;
        } else if (type == VFS_SYMLINK) {
            r.node->size = strnlen(tar->link_name, sizeof tar->link_name);
            r.node->target = kmalloc(r.node->size + 1);
            r.node->perms = mode;
            memcpy(r.node->target, tar->link_name, r.node->size);
            r.node->target[r.node->size] = 0;
            r.node->inode = inode++;
        }

    skip:
        tar = (struct tar *)((char *)tar + ((filesize + 511) / 512 + 1) * 512);
    }
    return 0;
}

void tar_module(struct tar *tar) {
    vfs_mount(vfs_get_root(), "tar", (vfs_node_t *)tar, 0);
}

void tar_initialize(void) {
    vfs_register(&tar_mount_ops);
}