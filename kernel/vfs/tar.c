#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/tar.h>
#include <kernel/vfs.h>
#include <limine.h>

vfs_node_t *tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type);

vfs_node_ops_t tar_ops = {
    .create = tar_create
};

vfs_node_t *tar_create(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    vfs_node_t *node = vfs_create_node(name, type);
    node->ops = &tar_ops;
    return vfs_add_node(parent, node);
}

int oct2bin(unsigned char *str, int size) {
    int n = 0;
    unsigned char *c = str;
    while (size-- > 0) {
        n *= 8;
        n += *c - '0';
        c++;
    }
    return n;
}

void tar_module(struct limine_file *mod) {
    vfs_get_root()->ops = &tar_ops;

    struct tar *tar = (struct tar *)mod->address;

    while (!memcmp(tar->ustar, "ustar", 5)) {
        int filesize = oct2bin((unsigned char *)tar->size, sizeof(tar->size));

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
                tar = (struct tar *)((unsigned char *)tar + ((filesize + 511) / 512 + 1) * 512);
                continue;
        }

        vfs_node_t *node = vfs_lookup(NULL, tar->name, true, type);
        if (!node) {
            dprintf(LOG_WARNING, "\033[93mtar:\033[0m failed to create %s\n", tar->name);
        }

        tar = (struct tar *)((unsigned char *)tar + ((filesize + 511) / 512 + 1) * 512);
    }

    vfs_print_tree(NULL);
}