#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/tar.h>
#include <kernel/vfs.h>
#include <limine.h>

vfs_node_t *tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type);
long tar_read(struct vfs_node *node, void *buffer, long offset, size_t len);

vfs_node_ops_t tar_ops = {
    .create = tar_create,
    .read = tar_read
};

vfs_node_t *tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    vfs_node_t *node = vfs_create_node(name, type);
    node->ops = &tar_ops;
    return vfs_add_node(parent, node);
}

long tar_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    // TODO: check for ustar signature
    size_t count = len < node->size - offset ? len : node->size - offset;
    memcpy(buffer, node->device + offset, count);
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
            node->device = (void *)((unsigned char *)tar + 512);
            node->size = filesize;
        }

        tar = (struct tar *)((char *)tar + ((filesize + 511) / 512 + 1) * 512);
    }

    //vfs_print_tree(NULL);
}