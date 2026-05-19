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
extern void random_initialize(void);
extern void tty_initialize(void);
extern void fbdev_initialize(void);
extern void tmpfs_initialize(void);
extern void procfs_initialize(void);
extern void pty_initialize(void);

vfs_node_t *vfs_root = NULL;
list_t *vfs_mount_ops = NULL;

vfs_node_t *vfs_get_root(void) {
    return vfs_root;
}

vfs_node_t *vfs_create_node(const char *name, enum vfs_node_type type) {
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    strncpy(node->name, name, MAX_PATH);
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
    node->device = NULL;
    node->target = NULL;
    node->mount = NULL;
    node->refcount = 0;
    return node;
}

vfs_node_t *vfs_create_symlink(const char *name, const char *target) {
    vfs_node_t *node = vfs_create_node(name, VFS_SYMLINK);
    node->target = strdup(target);
    node->size = strlen(target);
    return node;
}

vfs_node_t *vfs_add_node(vfs_node_t *parent, vfs_node_t *node) {
    if (!parent)
        parent = vfs_get_root();
    if (parent->type != VFS_DIRECTORY)
        return NULL;
    node->refcount++;
    node->parent = parent;
    list_insert(parent->children, node);
    return node;
}

long vfs_remove_node(vfs_node_t *node) {
    if (!node)
        return -EINVAL;
    
    if (node->parent) {
        list_remove_value(node->parent->children, node);
        node->refcount--;
    }

    if (node->refcount <= 0) {
        if (node->type == VFS_SYMLINK && node->target)
            kfree((void *)node->target);
        list_free(node->children);
        kfree(node);
    }
    return 0;
}

long vfs_remove(vfs_node_t *node) {
    if (!node)
        return -EINVAL;
    if (node->mount)
        return -EBUSY;
    if (!node->ops || !node->ops->remove)
        return -EINVAL;
    
    long ret = node->ops->remove(node);
    if (ret < 0)
        return ret;
    
    return vfs_remove_node(node);
}

long vfs_rename(vfs_node_t *node, vfs_node_t *cwd, const char *path) {
    if (!node || !cwd || !path)
        return -EINVAL;
    if (!node->parent)
        return -EINVAL;

    vfs_node_t *parent;
    const char *name;
    char *copy = strdup(path), *last_token = strrchr(copy, '/');
    
    if (last_token) {
        *last_token = '\0';
        name = last_token + 1;
        vfs_result_t r = vfs_lookup(cwd, *copy ? copy : "/", true, VFS_NONE);
        parent = r.node;
        if (!parent) {
            kfree(copy);
            return r.error;
        }
    } else {
        parent = cwd;
        name = copy;
    }

    if (!node->ops || !node->ops->rename)
        return -EINVAL;
    if (node->device != parent->device)
        return -EXDEV;

    vfs_result_t r = vfs_lookup(parent, name, true, VFS_NONE);
    vfs_node_t *target = r.node;
    if (target) {
        if (target->type == VFS_DIRECTORY && node->type != VFS_DIRECTORY)
            return -EISDIR;
        if (target->type != VFS_DIRECTORY && node->type == VFS_DIRECTORY)
            return -ENOTDIR;
        if (target->type == VFS_DIRECTORY && target->children->length > 0)
            return -ENOTEMPTY;
    }

    long ret = node->ops->rename(node, parent, name);
    if (ret < 0)
        return ret;

    list_remove_value(node->parent->children, node);
    strcpy(node->name, name);
    node->parent = parent;
    list_insert(parent->children, node);

    return 0;
}

vfs_result_t vfs_resolve_symlink(vfs_node_t *node, int depth) {
    if (!node || node->type != VFS_SYMLINK || !node->target || depth <= 0)
        return (vfs_result_t){ NULL, -EINVAL };
    if (!node->symlink) {
        vfs_result_t r = vfs_lookup(node->parent, node->target, true, VFS_NONE);
        if (!r.node)
            return r;
        node->symlink = r.node;
    }
    if (node->symlink->type == VFS_SYMLINK)
        return vfs_resolve_symlink(node->symlink, depth - 1);
    return (vfs_result_t){ node->symlink, 0 };
}

vfs_result_t vfs_find_child(vfs_node_t *parent, const char *name, bool follow_symlinks) {
    foreach(item, parent->children) {
        vfs_node_t *child = item->value;
        if (!strcmp(child->name, name)) {
            if (child->type == VFS_SYMLINK && follow_symlinks)
                return vfs_resolve_symlink(child, MAX_SYMLINKS);
            return (vfs_result_t){ child, 0 };
        }
    }
    return (vfs_result_t){ NULL, -ENOENT };
}

vfs_result_t vfs_touch(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    return (parent->ops && parent->ops->create) ? parent->ops->create(parent, name, type) : (vfs_result_t){ NULL, -EINVAL };
}

vfs_result_t vfs_lookup(vfs_node_t *cwd, const char *path, bool follow_symlinks, enum vfs_node_type create_type) {
    if (!path) return (vfs_result_t){ NULL, -ENOENT };
    if (!cwd || path[0] == '/') cwd = vfs_get_root();

    char *copy = strdup(path);
    char *saveptr;
    char *token = strtok_r(copy, "/", &saveptr), *next = strtok_r(NULL, "/", &saveptr);
    vfs_node_t *node = cwd;
    
    while (token) {
        if (!strcmp(token, ".")) {
            token = next;
            next = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        if (!strcmp(token, "..")) {
            if (node->parent)
                node = node->parent;
            token = next;
            next = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        if (node->type == VFS_DIRECTORY && node->ops && node->ops->open)
            node->ops->open(node, O_RDONLY);
        
        vfs_result_t r = vfs_find_child(node, token, follow_symlinks);
        if (!r.node && r.error != -ENOENT) {
            kfree(copy);
            return r;
        }
        vfs_node_t *child = r.node;
        if (!child) {
            if (create_type != VFS_NONE && !next && node->type == VFS_DIRECTORY) {
                r = vfs_touch(node, token, create_type);
                if (!r.node) {
                    kfree(copy);
                    return r;
                }
                child = r.node;
            } else {
                kfree(copy);
                return (vfs_result_t){ NULL, -ENOENT };
            }
        }
        node = child;

        token = next;
        next = strtok_r(NULL, "/", &saveptr);
    }

    kfree(copy);
    return (vfs_result_t){ node, 0 };
}

vfs_result_t vfs_open(vfs_node_t *cwd, const char *path, long flags) {
    vfs_result_t r = vfs_lookup(cwd, path, true, (flags & O_CREAT) ? VFS_FILE : VFS_NONE);
    if (!r.node)
        return r;
    vfs_node_t *node = r.node;
    if (!node->ops || !node->ops->open) {
        node->refcount++;
        return (vfs_result_t){ node, 0 };
    }
    r = node->ops->open(node, flags);
    node = r.node;
    if (node)
        node->refcount++;
    return r;
}

long vfs_close(vfs_node_t *node) {
    if (!node)
        return -ENOENT;

    acquire(&node->waiters_lock);
    foreach_safe(i, node->waiters) {
        struct thread *tcb = i->value;
        if (tcb == this)
            list_remove(node->waiters, i);
    }
    release(&node->waiters_lock);
    
    node->refcount--;
    if (!node->ops || !node->ops->close)
        return 0;
    return node->ops->close(node);
}

long vfs_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    if (!buffer)
        return -EFAULT;
    if (!node)
        return -ENOENT;
    if (node->type == VFS_DIRECTORY)
        return -EISDIR;
    if (node->ops && node->ops->read)
        return node->ops->read(node, buffer, offset, len);
    return -EPERM;
}

long vfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    if (!buffer)
        return -EFAULT;
    if (!node)
        return -ENOENT;
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
            release(&node->waiters_lock);
            sched_block(this, 0);
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
        sched_block(this, timeout);
    }
    acquire(&node->waiters_lock);
    list_remove_value(node->waiters, this);
    release(&node->waiters_lock);
    return node->ops->poll(node, events);
}

long vfs_poll_multiplexed(vfs_node_t **nodes, short *events, short *revents, long nfds, long timeout) {
    if (!nfds)
        return -EINVAL;
    if (!nodes || !events || !revents)
        return -EFAULT;

    for (int fd = 0; fd < nfds; fd++) {
        vfs_node_t *node = nodes[fd];
        if (!node->ops || !node->ops->poll || !node->waiters) {
            revents[fd] = -1;
            continue;
        }

        acquire(&node->waiters_lock);
        list_insert(node->waiters, this);
        release(&node->waiters_lock);
    }

    size_t start;
    uptime(NULL, &start);

    int ready = 0;
    for (;;) {
        ready = 0;
        for (int fd = 0; fd < nfds; fd++) {
            vfs_node_t *node = nodes[fd];
            if (!node->ops || !node->ops->poll)
                continue;

            if ((revents[fd] = node->ops->poll(node, events[fd])))
                ready++;
        }

        if (ready || !timeout)
            break;

        if (timeout > 0) {
            size_t now;
            uptime(NULL, &now);
            long remaining = timeout - (now - start);
            if (remaining <= 0)
                break;
            sched_block(this, remaining);
        } else {
            sched_block(this, 0);
        }
    }

    ready = 0;
    for (int fd = 0; fd < nfds; fd++) {
        vfs_node_t *node = nodes[fd];

        acquire(&node->waiters_lock);
        list_remove_value(node->waiters, this);
        release(&node->waiters_lock);

        if (revents[fd])
            ready++;
    }

    return ready;
}

void vfs_wake_waiters(vfs_node_t *node) {
    if (!node)
        return;
    
    acquire(&node->waiters_lock);
    foreach_safe(i, node->waiters) {
        struct thread *tcb = i->value;
        sched_wake(tcb);
    }
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

void vfs_register(vfs_mount_ops_t *ops) {
    list_insert(vfs_mount_ops, ops);
}

void vfs_unregister(vfs_mount_ops_t *ops) {
    list_remove_value(vfs_mount_ops, ops);
}

long vfs_mount(vfs_node_t *node, const char *type, vfs_node_t *device, long flags) {
    if (!node || node->type != VFS_DIRECTORY || !type)
        return -EINVAL;
    if (node->mount)
        return -EBUSY;

    foreach(i, vfs_mount_ops) {
        vfs_mount_ops_t *ops = i->value;
        if (strcmp(ops->type, type))
            continue;
        if (!ops->nodev && !device)
            return -EINVAL;

        long ret = ops->mount(node, device, flags);
        if (ret < 0)
            return ret;

        vfs_mountpoint_t *mp = kmalloc(sizeof(vfs_mountpoint_t));
        mp->ops = ops;
        mp->node = node;
        mp->device = device;
        mp->flags = flags;
        node->mount = mp;

        return 0;
    }

    return -EINVAL;
}

long vfs_unmount(vfs_node_t *node, long flags) {
    if (!node)
        return -ENOENT;

    vfs_mountpoint_t *mnt = node->mount;
    if (!mnt || !mnt->ops->unmount)
        return -EINVAL;

    long ret = mnt->ops->unmount(node, flags);
    if (ret < 0)
        return ret;

    node->mount = NULL;
    kfree(mnt);
    
    return 0;
}

long vfs_chmod(vfs_node_t *node, unsigned int mode) {
    if (!node || !node->ops || !node->ops->chmod)
        return -EINVAL;

    long ret = node->ops->chmod(node, mode);
    if (ret < 0)
        return ret;

    node->perms = mode;
    return 0;
}

long vfs_link(vfs_node_t *old_node, vfs_node_t *new_node) {
    if (!old_node || old_node->type == VFS_SYMLINK || !old_node->ops || !old_node->ops->link || !new_node)
        return -EOPNOTSUPP;

    return old_node->ops->link(old_node, new_node);
}

long vfs_truncate(vfs_node_t *node, size_t length) {
    if (!node || !node->ops || !node->ops->truncate)
        return -EOPNOTSUPP;

    long ret = node->ops->truncate(node, length);
    if (ret < 0)
        return ret;

    node->size = length;
    return 0;
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
    vfs_mount_ops = list_create();

    devfs_initialize();
    zero_initialize();
    random_initialize();
    tty_initialize();
    fbdev_initialize();
    tmpfs_initialize();
    procfs_initialize();
    pty_initialize();

    dprintf(LOG_INFO, "\033[93mvfs:\033[0m initialized VFS\n");
}