#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/tar.h>
#include <kernel/vfs.h>
#include <limine.h>

vfs_node_t *tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type);
long tar_read(vfs_node_t *node, void *buffer, long offset, size_t len);

vfs_ops_t tar_ops = {
    .create = tar_create,
    .read = tar_read
};

vfs_node_t *tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    vfs_node_t *node = vfs_create_node(name, type);
    node->ops = &tar_ops;
    return vfs_add_node(parent, node);
}

long tar_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    struct tar *tar = (struct tar *)node->device;
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

void tar_module(struct limine_file *mod) {
    dprintf(LOG_INFO, "\033[93mtar:\033[0m mounting %s\n", mod->path);
    vfs_get_root()->ops = &tar_ops;

    struct tar *tar = (struct tar *)mod->address;

    while (!memcmp(tar->ustar, "ustar", 5)) {
        int filesize = oct2bin(tar->size, sizeof(tar->size));

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
            default:
                dprintf(LOG_WARNING, "\033[93mtar:\033[0m skipping '%s': unsupported type %c\n", tar->name, tar->type);
                tar = (struct tar *)((char *)tar + ((filesize + 511) / 512 + 1) * 512);
                continue;
        }

        vfs_node_t *node = vfs_lookup(NULL, tar->name, true, type);
        if (!node) {
            dprintf(LOG_WARNING, "\033[93mtar:\033[0m failed to create %s\n", tar->name);
        } else if (type == VFS_FILE) {
            // node->device = (void *)((unsigned char *)tar + 512);
            node->device = tar;
            node->size = filesize;
            node->perms = oct2bin(tar->mode + 4, 3);
        }

        tar = (struct tar *)((char *)tar + ((filesize + 511) / 512 + 1) * 512);
    }

    //vfs_print_tree(NULL);
    vfs_get_root()->ops = NULL;
}