#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/vfs.h>

vfs_node_t *vfs_root = NULL;

struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type) {
    struct vfs_node *node = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
    strcpy(node->name, name);
    node->open = false;
    node->type = type;
    node->size = 0;
    node->blocks = 0;
    node->perms = type == VFS_DIRECTORY ? 0755 : 0644;
    node->inode = 0;
    node->flags = 0;
    node->parent = NULL;
    node->symlink = NULL;
    node->atime = node->ctime = node->mtime = 0;
    node->children = list_create();
    node->device = NULL;
    return node;
}

struct vfs_node *vfs_add_node(struct vfs_node *parent, struct vfs_node *node) {
    if (!parent)
        parent = vfs_root;
    node->parent = parent;
    return list_insert(parent->children, node)->value;
}

long vfs_remove_node(struct vfs_node *node) {
    if (!node)
        return -EINVAL;
    if (node->open)
        return -EBUSY;
    if (node->type == VFS_DIRECTORY && node->children->length)
        return -ENOTEMPTY;
    if (!node->ops)
        return -EPERM;

    long ret = node->ops->remove ? node->ops->remove(node) : -EPERM;
    if (ret < 0) return ret;
    
    if (node->parent)
        list_remove_value(node->parent->children, node);
    if (node->type == VFS_SYMLINK && node->device)
        kfree(node->device);

    list_free(node->children);
    kfree(node);
    return 0;
}

struct vfs_node *vfs_lookup(struct vfs_node *cwd, const char *path, bool follow_symlinks) {
    if (!path) return NULL;
    if (!cwd) cwd = vfs_root;

    char *copy = strdup(path);
    char *token = strtok(copy, "/");
    struct vfs_node *node = cwd;
    
    while (token) {
        // if (node->type == VFS_DIRECTORY && node->ops && node->ops->open)
        //     node->ops->open(node, 0);
        
        struct vfs_node *child = vfs_find_child(node, token, follow_symlinks);
        if (!child) {
            kfree(copy);
            return node;
        }
        node = child;

        token = strtok(NULL, "/");
    }

    kfree(copy);
    return node;
}

void vfs_install(void) {
    vfs_root = vfs_create_node("", VFS_DIRECTORY);
    vfs_root->open = true;

    dprintf(LOG_INFO, "\033[93mvfs:\033[0m initialized VFS\n");
}