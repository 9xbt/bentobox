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
extern void tar_initialize(void);

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
    acquire(&parent->children->lock);
    list_insert(parent->children, node);
    release(&parent->children->lock);
    return node;
}

long vfs_remove_node(vfs_node_t *node) {
    if (!node)
        return -EINVAL;
    
    if (node->parent) {
        acquire(&node->parent->children->lock);
        list_remove_value(node->parent->children, node);
        release(&node->parent->children->lock);
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

    vfs_node_t *old_parent = node->parent;

    acquire(&node->parent->children->lock);
    if (old_parent != parent)
        acquire(&parent->children->lock);

    list_remove_value(node->parent->children, node);
    strcpy(node->name, name);
    node->parent = parent;
    list_insert(parent->children, node);

    if (old_parent != parent)
        release(&parent->children->lock);
    release(&old_parent->children->lock);
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
    acquire(&parent->children->lock);
    foreach(item, parent->children) {
        vfs_node_t *child = item->value;
        if (!strcmp(child->name, name)) {
            release(&parent->children->lock);
            if (child->type == VFS_SYMLINK && follow_symlinks)
                return vfs_resolve_symlink(child, MAX_SYMLINKS);
            return (vfs_result_t){ child, 0 };
        }
    }
    release(&parent->children->lock);
    return (vfs_result_t){ NULL, -ENOENT };
}

vfs_result_t vfs_touch(vfs_node_t *parent, const char *name, enum vfs_node_type type) {
    return (parent->ops && parent->ops->create) ? parent->ops->create(parent, name, type) : (vfs_result_t){ NULL, -EINVAL };
}

vfs_result_t vfs_lookup(vfs_node_t *cwd, const char *path, bool follow_symlinks, enum vfs_node_type create_type) {
    if (!path)
        return (vfs_result_t){ NULL, -ENOENT };
    if (!cwd || path[0] == '/')
        cwd = vfs_get_root();

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
    
    node->refcount--;
    if (!node->ops || !node->ops->close)
        return 0;
    long ret = node->ops->close(node);

    if (this_proc) {
        acquire(&node->waiters->lock);
        if (node->waiters->length) {
            acquire(&this_proc->threads->lock);
            foreach(j, this_proc->threads) {
                struct thread *tcb = j->value;
                list_remove_value(node->waiters, tcb);
            }
            release(&this_proc->threads->lock);
        }
        release(&node->waiters->lock);
    }

    return ret;
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
    if (node->parent && node->parent->mount && node->parent->mount->ops->readonly)
        return -EROFS;
    if (node->ops && node->ops->write)
        return node->ops->write(node, buffer, offset, len);
    return -EPERM;
}

long vfs_poll(vfs_node_t *node, long events, long timeout) {
    if (!node)
        return -ENOENT;
    if (!node->ops || !node->ops->poll)
        return -1UL;

    node_t *item = list_create_node(this);
    acquire(&node->waiters->lock);
    __atomic_add_fetch(&this->refcount, 1, __ATOMIC_ACQ_REL);
    list_append(node->waiters, item);
    release(&node->waiters->lock);

    size_t sec, nsec;
    uptime(&sec, &nsec);
    size_t start = sec * 1000000000 + nsec;

    long poll;
    for (;;) {
        poll = node->ops->poll(node, events);
        if (poll || !timeout)
            break;

        if (timeout > 0) {
            uptime(&sec, &nsec);
            size_t now = sec * 1000000000 + nsec;
            
            long remaining = timeout - (now - start);
            if (remaining <= 0)
                break;
            sched_block(this, remaining);
        } else {
            sched_block(this, 0);
        }
    }

    acquire(&node->waiters->lock);
    list_unlink(node->waiters, item);
    __atomic_sub_fetch(&this->refcount, 1, __ATOMIC_ACQ_REL);
    release(&node->waiters->lock);
    kfree(item);
    return poll;
}

long vfs_poll_multiplexed(vfs_node_t **nodes, short *events, short *revents, long nfds, long timeout) {
    if (!nfds)
        return -EINVAL;
    if (!nodes || !events || !revents)
        return -EFAULT;

    for (int fd = 0; fd < nfds; fd++) {
        vfs_node_t *node = nodes[fd];
        if (!node || !node->ops || !node->ops->poll) {
            revents[fd] = 0;
            continue;
        }

        node_t *item = list_create_node(this);
        acquire(&node->waiters->lock);
        __atomic_add_fetch(&this->refcount, 1, __ATOMIC_ACQ_REL);
        list_append(node->waiters, item);
        release(&node->waiters->lock);
    }

    size_t sec, nsec;
    uptime(&sec, &nsec);
    size_t start = sec * 1000000000 + nsec;

    int ready = 0;
    for (;;) {
        ready = 0;
        for (int fd = 0; fd < nfds; fd++) {
            vfs_node_t *node = nodes[fd];
            if (!node || !node->ops || !node->ops->poll)
                continue;

            if ((revents[fd] = node->ops->poll(node, events[fd])))
                ready++;
        }

        if (ready || !timeout)
            break;

        if (timeout > 0) {
            uptime(&sec, &nsec);
            size_t now = sec * 1000000000 + nsec;

            long remaining = timeout - (now - start);
            if (remaining <= 0)
                break;
            sched_block(this, remaining);
        } else {
            sched_block(this, 0);
        }
    }

    for (int fd = 0; fd < nfds; fd++) {
        vfs_node_t *node = nodes[fd];

        acquire(&node->waiters->lock);
        node_t *item = list_find(node->waiters, this);
        list_unlink(node->waiters, item);
        __atomic_sub_fetch(&this->refcount, 1, __ATOMIC_ACQ_REL);
        release(&node->waiters->lock);
        kfree(item);
    }

    return ready;
}

void vfs_wake_waiters(vfs_node_t *node) {
    if (!node)
        return;
    
    acquire(&node->waiters->lock);
    foreach(i, node->waiters) {
        struct thread *tcb = i->value;
        sched_wake(tcb);
        if (__atomic_load_n(&tcb->refcount, __ATOMIC_ACQUIRE) <= 1) {
            if (__atomic_sub_fetch(&tcb->refcount, 1, __ATOMIC_ACQ_REL) == 0) {
                list_remove(node->waiters, i);
                sched_clean_tcb(tcb);
            }
        }
    }
    release(&node->waiters->lock);
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
    acquire(&vfs_mount_ops->lock);
    list_insert(vfs_mount_ops, ops);
    release(&vfs_mount_ops->lock);
}

void vfs_unregister(vfs_mount_ops_t *ops) {
    acquire(&vfs_mount_ops->lock);
    list_remove_value(vfs_mount_ops, ops);
    release(&vfs_mount_ops->lock);
}

long vfs_mount(vfs_node_t *node, const char *type, vfs_node_t *device, long flags) {
    if (!node || node->type != VFS_DIRECTORY || !type)
        return -EINVAL;
    if (node->mount)
        return -EBUSY;

    acquire(&vfs_mount_ops->lock);
    foreach(i, vfs_mount_ops) {
        vfs_mount_ops_t *ops = i->value;
        if (strcmp(ops->type, type))
            continue;
        if (!ops->nodev && !device) {
            release(&vfs_mount_ops->lock);
            return -EINVAL;
        }
        release(&vfs_mount_ops->lock);

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
    release(&vfs_mount_ops->lock);

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
    tar_initialize();

    dprintf(LOG_INFO, "\033[93mvfs:\033[0m initialized VFS\n");
}