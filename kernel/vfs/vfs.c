#include <kernel/spinlock.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/vfs.h>

extern void devfs_initialize(void);
extern void zero_initialize(void);
extern void tty_initialize(void);
extern void fbdev_initialize(void);
extern void tmpfs_initialize(void);
extern void procfs_initialize(void);

vfs_node_t *vfs_root = NULL;

vfs_node_t *vfs_get_root(void) {
    return vfs_root;
}

vfs_node_t *vfs_create_node(const char *name, enum vfs_node_type type) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    strcpy(node->name, name);
    node->busy = false;
    node->type = type;
    node->size = 0;
    node->blocks = 0;
    node->perms = type == VFS_DIRECTORY ? 0755 : 0644;
    node->inode = 0;
    node->flags = 0;
    node->atime = node->ctime = node->mtime = now();
    node->children = list_create();
    node->waiters = list_create();
    node->waiters_lock = 0;
    node->parent = NULL;
    node->symlink = NULL;
    node->ops = NULL;
    node->tty_ops = NULL;
    node->device = NULL;
    return node;
}

vfs_node_t *vfs_add_node(vfs_node_t *parent, vfs_node_t *node) {
    if (!parent)
        parent = vfs_get_root();
    if (parent->type != VFS_DIRECTORY)
        return NULL;
    node->parent = parent;
    //node->ops = parent->ops;
    return list_insert(parent->children, node)->value;
}

long vfs_remove(vfs_node_t *node) {
    if (!node)
        return -EINVAL;
    if (node->busy)
        return -EBUSY;
    if (node->type == VFS_DIRECTORY && node->children->length)
        return -ENOTEMPTY;
    if (!node->ops || !node->ops->remove)
        return -EINVAL;

    long ret = node->ops->remove(node);
    if (ret < 0)
        return ret;
    
    if (node->parent)
        list_remove_value(node->parent->children, node);
    if (node->type == VFS_SYMLINK && node->device)
        kfree(node->device);

    list_free(node->children);
    kfree(node);
    return 0;
}

long vfs_rename(vfs_node_t *node, const char *name) {
    if (!node)
        return -EINVAL;
    if (!node->ops || !node->ops->remove)
        return -EINVAL;

    long ret = node->ops->rename(node, name);
    if (ret < 0)
        return ret;

    strcpy(node->name, name);
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
    if (!cwd || path[0] == '/') cwd = vfs_get_root();

    char *copy = strdup(path);
    char *token = strtok(copy, "/"), *next = strtok(NULL, "/");
    vfs_node_t *node = cwd;
    
    while (token) {
        if (!strcmp(token, ".")) {
            token = next;
            next = strtok(NULL, "/");
            continue;
        }

        if (!strcmp(token, "..")) {
            if (node->parent) node = node->parent;
            token = next;
            next = strtok(NULL, "/");
            continue;
        }

        if (node->type == VFS_DIRECTORY && node->ops && node->ops->open)
            node->ops->open(node, O_RDONLY);
        
        vfs_node_t *child = vfs_find_child(node, token, follow_symlinks);
        if (!child) {
            if (create_type != VFS_NONE && !next && node->type == VFS_DIRECTORY) {
                //node->ops = vfs_get_root()->ops;
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
    if (node->ops && node->ops->open && node->ops->open(node, flags) < 0)
        return NULL;
    return node;
}

long vfs_close(vfs_node_t *node) {
    if (!node)
        return -ENOENT;
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

long vfs_poll(vfs_node_t *node, long events, long timeout) {
    if (!node)
        return -ENOENT;
    if (!node->ops || !node->ops->poll || !node->waiters)
        return -1UL;

    long poll = node->ops->poll(node, events);
    if (poll)
        return poll;
    if (timeout == 0)
        return 0;

    acquire(&node->waiters_lock);
    list_insert(node->waiters, this);
    poll = node->ops->poll(node, events);
    if (poll) {
        list_remove_value(node->waiters, this);
        release(&node->waiters_lock);
        return poll;
    }
    
    if (timeout == -1) {
        for (;;) {
            this->state = THREAD_PAUSED;
            release(&node->waiters_lock);
            sched_yield();
            acquire(&node->waiters_lock);
            
            poll = node->ops->poll(node, events);
            if (poll) {
                list_remove_value(node->waiters, this);
                release(&node->waiters_lock);
                return poll;
            }
        }
    } else {
        release(&node->waiters_lock);
        sched_sleep(timeout);
    }
    return node->ops->poll(node, events);
}

void vfs_wake_waiters(vfs_node_t *node) {
    if (!node)
        return;
    acquire(&node->waiters_lock);
    foreach(i, node->waiters) {
        struct thread *tcb = i->value;
        tcb->state = THREAD_RUNNING;
    }
    list_clear(node->waiters);
    release(&node->waiters_lock);
}

char *vfs_resolve_path(vfs_node_t *node) {
    if (!node) node = vfs_get_root();
    if (node == vfs_get_root()) return strdup("/");

    char path[MAX_PATH] = "";
    vfs_node_t *current = node;

    while (current != NULL) {
        snprintf(path, sizeof path, "%s%s%s", current == vfs_get_root() ? "" : "/", current->name, path);
        current = current->parent;
    }

    return strdup(path);
}

void vfs_print_tree(vfs_node_t *node) {
    if (!node)
        node = vfs_get_root();
    if (node == vfs_get_root())
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

    devfs_initialize();
    zero_initialize();
    tty_initialize();
    fbdev_initialize();
    tmpfs_initialize();
    procfs_initialize();

    dprintf(LOG_INFO, "\033[93mvfs:\033[0m initialized VFS\n");
}