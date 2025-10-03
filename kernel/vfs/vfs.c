#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/vfs.h>

extern void tty_initialize(void);

vfs_node_t *vfs_root = NULL;

vfs_node_t *vfs_create_node(const char *name, enum vfs_node_type type) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    strcpy(node->name, name);
    node->open = false;
    node->busy = false;
    node->type = type;
    node->size = 0;
    node->blocks = 0;
    node->perms = type == VFS_DIRECTORY ? 0755 : 0644;
    node->inode = 0;
    node->flags = 0;
    node->atime = node->ctime = node->mtime = 0;
    node->children = list_create();
    node->parent = NULL;
    node->symlink = NULL;
    node->ops = NULL;
    node->tty_ops = NULL;
    node->device = NULL;
    return node;
}

vfs_node_t *vfs_add_node(vfs_node_t *parent, vfs_node_t *node) {
    if (!parent)
        parent = vfs_root;
    if (parent->type != VFS_DIRECTORY)
        return NULL;
    node->parent = parent;
    return list_insert(parent->children, node)->value;
}

long vfs_remove_node(vfs_node_t *node) {
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

vfs_node_t *vfs_find_child(vfs_node_t *parent, const char *name, bool follow_symlinks) {
    (void)follow_symlinks;
    foreach(item, parent->children) {
        vfs_node_t *child = item->value;
        if (!strcmp(child->name, name)) {
            return child;
        }
    }
    return NULL;
}

vfs_node_t *vfs_touch(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    return parent->ops && parent->ops->create ? parent->ops->create(parent, name, type) : NULL;
}

vfs_node_t *vfs_lookup(vfs_node_t *cwd, const char *path, bool follow_symlinks, enum vfs_node_type create_type) {
    if (!path) return NULL;
    if (!cwd) cwd = vfs_root;

    char *copy = strdup(path);
    char *token = strtok(copy, "/"), *next = strtok(NULL, "/");
    vfs_node_t *node = cwd;
    
    while (token) {
        if (strcmp(token, ".") == 0) {
            token = next;
            next = strtok(NULL, "/");
            continue;
        }

        if (strcmp(token, "..") == 0) {
            if (node->parent) node = node->parent;
            token = next;
            next = strtok(NULL, "/");
            continue;
        }

        if (node->type == VFS_DIRECTORY && node->ops && node->ops->open)
            node->ops->open(node, O_RDONLY);
        
        vfs_node_t *child = vfs_find_child(node, token, follow_symlinks);
        if (!child) {
            if (create_type != VFS_NONE && !next) {
                child = vfs_touch(node, token, create_type);
                if (!child) {
                    kfree(copy);
                    return NULL;
                }
            } else {
                kfree(copy);
                return NULL;
            }
        }
        node = child;

        token = next;
        next = strtok(NULL, "/");
    }

    kfree(copy);
    return node;
}

vfs_node_t *vfs_open(vfs_node_t *cwd, const char *path, long flags) {
    vfs_node_t *node = vfs_lookup(cwd, path, true, (flags & O_CREAT) ? VFS_FILE : VFS_NONE);
    if (!node)
        return NULL;
    if (node->open)
        return NULL;
    if (node->ops && node->ops->open)
        node->ops->open(node, flags);
    return node;
}

long vfs_close(vfs_node_t *node) {
    if (!node)
        return -ENOENT;
    if (node->open)
        return -EBUSY;
    if (node->ops && node->ops->close)
        return node->ops->close(node);
    return 0;
}

long vfs_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    if (!buffer)
        return -EFAULT;
    if (!node)
        return -ENOENT;
    if (node->busy)
        return -EBUSY;
    if (node->type == VFS_DIRECTORY)
        return -EISDIR;
    if (node->ops && node->ops->read)
        return node->ops->read(node, buffer, offset, len);
    return -EPERM;
}

long vfs_write(vfs_node_t *node, void *buffer, long offset, size_t len) {
    if (!buffer)
        return -EFAULT;
    if (!node)
        return -ENOENT;
    if (node->busy)
        return -EBUSY;
    if (node->type == VFS_DIRECTORY)
        return -EISDIR;
    if (node->ops && node->ops->write)
        return node->ops->write(node, buffer, offset, len);
    return -EPERM;
}

char *vfs_resolve_path(vfs_node_t *node) {
    if (!node) node = vfs_root;
    if (node == vfs_root) return strdup("/");

    char path[MAX_PATH] = "";
    vfs_node_t *current = node;

    while (current != NULL) {
        snprintf(path, sizeof path, "%s%s%s", current == vfs_root ? "" : "/", current->name, path);
        current = current->parent;
    }

    return strdup(path);
}

vfs_node_t *vfs_get_root(void) {
    return vfs_root;
}

void vfs_print_tree(vfs_node_t *node) {
    if (!node)
        node = vfs_root;
    if (node == vfs_root)
        dprintf(LOG_DEBUG, "/\n");

    foreach(i, node->children) {
        vfs_node_t *child = i->value;

        char *path = vfs_resolve_path(child);
        dprintf(LOG_DEBUG, "%s\n", path);

        if (child->children->length > 0) {
            vfs_print_tree(child);
        }

        kfree(path);
    }
}

void vfs_install(void) {
    vfs_root = vfs_create_node("", VFS_DIRECTORY);
    vfs_root->open = true;

    vfs_add_node(NULL, vfs_create_node("dev", VFS_DIRECTORY));

    tty_initialize();

    dprintf(LOG_INFO, "\033[93mvfs:\033[0m initialized VFS\n");
}