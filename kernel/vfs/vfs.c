#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/spinlock.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

extern void devfs_initialize(void);
extern void zero_initialize(void);
extern void ps2_initialize(void);
extern void serial_initialize(void);
extern void tmpfs_initialize(void);
extern void tty_initialize(void);
extern void fbdev_initialize(void);
extern void procfs_initialize(void);

extern struct vfs_node *tmpfs_create_file(struct vfs_node *parent, const char *name);
extern long tmpfs_remove_file(struct vfs_node *node);

struct vfs_node *vfs_root = NULL, *vfs_devfs = NULL;

struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type) {
    struct vfs_node *node = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
    strcpy(node->name, name);
    node->busy = false;
    node->type = type;
    node->size = 0;
    node->perms = type == VFS_DIRECTORY ? 0755 : 0644;
    node->inode = 0;
    node->parent = NULL;
    node->children = list_create();
    node->read = NULL;
    node->write = NULL;
    node->symlink = NULL;
    node->isatty = false;
    node->tty_ops.ioctl = NULL;
    node->mmap = NULL;
    node->close = NULL;
    node->poll_list = list_create();
    node->poll = NULL;
    return node;
}

void vfs_add_node(struct vfs_node *root, struct vfs_node *node) {
    if (!root)
        root = vfs_root;
    node->parent = root;
    list_insert(root->children, node);
}

int vfs_remove_node(struct vfs_node *node) {
    if (!node)
        return -EINVAL;
    if (node == vfs_root)
        return -EBUSY;
    if (node->busy)
        return -EBUSY;
    if (node->type == VFS_DIRECTORY && node->children->head)
        return -ENOTEMPTY;
    
    if (node->parent) {
        uint8_t parent_perms = (node->parent->perms >> 6) & 0x7;
        if (!(parent_perms & 0x2)) {
            return -EACCES;
        }
    }

    long ret = node->type == VFS_DIRECTORY
        ? (node->driver.rmdir ? node->driver.rmdir(node) : 0)
        : (node->driver.remove ? node->driver.remove(node) : 0);
    if (ret < 0) return ret;
    
    if (node->parent)
        list_remove_value(node->parent->children, node);
    if (node->type == VFS_SYMLINK && node->device)
        kfree(node->device);
    list_free(node->children);
    list_free(node->poll_list);
    kfree(node);
    return 0;
}

void vfs_add_device(struct vfs_node *node) {
    vfs_add_node(vfs_devfs, node);
}

struct vfs_node *vfs_create_symlink(const char *name, const char *target) {
    struct vfs_node *node = vfs_create_node(name, VFS_SYMLINK);
    if (node && target) {
        node->symlink = vfs_open(NULL, target, false, false);
        if (!node->symlink) {
            node->device = kmalloc(strlen(target) + 1);
            strcpy(node->device, target);
        }
        node->size = strlen(target);
    }
    return node;
}

struct vfs_node *vfs_resolve_symlink(struct vfs_node *symlink, int max_depth) {
    if (!symlink || symlink->type != VFS_SYMLINK || max_depth <= 0)
        return NULL;
    if (!symlink->symlink) {
        if (!(symlink->symlink = vfs_open(NULL, symlink->device, false, false))) {
            return NULL;
        }
    }
    if (symlink->symlink->type == VFS_SYMLINK)
        return vfs_resolve_symlink(symlink, max_depth - 1);
    return symlink->symlink;
}

static struct vfs_node *vfs_find_child(struct vfs_node *parent, const char *name) {
    foreach(item, parent->children) {
        struct vfs_node *child = item->value;
        if (!strcmp(child->name, name)) {
            return (child->type == VFS_SYMLINK) ? vfs_resolve_symlink(child, MAX_NESTED_SYMLINKS) : child;
        }
    }
    return NULL;
}

static struct vfs_node *vfs_touch(struct vfs_node *parent, const char *name, bool isdir) {
    return isdir
        ? parent->driver.mkdir ? parent->driver.mkdir(parent, name) : NULL
        : (parent->driver.create ? parent->driver.create(parent, name) : NULL);
}

struct vfs_node* vfs_open(struct vfs_node *current, const char *path, bool create, bool isdir) {
    if (!path) return NULL;
    if (path[0] == '/' || !current) current = vfs_root;

    if (!strcmp(path, ".")) return current;
    if (!strcmp(path, "..")) return current->parent;

    char *copy = kmalloc(strlen(path) + 1);
    strcpy(copy, path);
    
    struct vfs_node *node = current;
    char *token = strtok(copy, "/");
    
    while (token) {
        if (!strcmp(token, ".")) {
            /* skip current directory */
        } else if (!strcmp(token, "..")) {
            node = node->parent ?: node;
        } else {
            struct vfs_node *child = vfs_find_child(node, token);
            if (!child) {
                struct vfs_node *file = create ? vfs_touch(node, token, isdir) : NULL;
                kfree(copy);
                return file;
            }
            node = child;
        }
        token = strtok(NULL, "/");
    }
    
    kfree(copy);
    return node;
}

int vfs_close(struct vfs_node *node) {
    if (node->busy)
        return -EBUSY;
    if (node->close)
        return node->close(node);
    return 0;
}

void vfs_resolve_path(char *s, struct vfs_node *node) {
    if (!node) node = vfs_root;

    char path[MAX_PATH] = "";
    struct vfs_node *current = node;

    while (current != NULL) {
        sprintf(path, "%s%s%s", current == vfs_root ? "" : "/", current->name, path);
        current = current->parent;
    }

    strcpy(s, path);
}

long vfs_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!buffer) return -EFAULT;
    if (!node) return -ENOENT;
    if (node->busy) return -EBUSY;
    if (node->read) {
        long ret = node->read(node, buffer, offset, len);
        return ret;
    }
    return -EINVAL;
}

long vfs_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!buffer) return -EFAULT;
    if (!node) return -ENOENT;
    if (node->busy) return -EBUSY;
    if (node->write) {
        long ret = node->write(node, buffer, offset, len);
        return ret;
    }
    return -EINVAL;
}

long vfs_poll(struct vfs_node *node, long events, long timeout) {
    if (!node || !node->poll || !node->poll_list)
        return -1UL;
    long poll = node->poll(node, events);
    if (poll)
        return poll;
    if (timeout == 0)
        return 0;
    list_insert(node->poll_list, this);
    if (timeout == -1) sched_block(TASK_POLLING);
    else sched_sleep(timeout * 1000);
    return node->poll(node, events);
}

void vfs_unblock_polling(struct vfs_node *node) {
    if (!node) return;
    foreach(proc, node->poll_list) {
        sched_unblock(proc->value);
    }
    list_empty(node->poll_list);
}

long vfs_check_perms(struct vfs_node *node, int mode) {
    if (!node)
        return -ENOENT;
    if (mode == F_OK)
        return 0;
    if (mode & R_OK && !(node->perms & (S_IRUSR | S_IRGRP | S_IROTH)))
        return -EACCES;
    if (mode & W_OK && !(node->perms & (S_IWUSR | S_IWGRP | S_IWOTH)))
        return -EACCES;
    if (mode & X_OK && !(node->perms & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return -EACCES;
    return 0;
}

static unsigned int convert_mode(enum vfs_node_type type, uint16_t perms) {
    unsigned int mode = 0;
    
    switch (type) {
        case VFS_FILE:
            mode |= S_IFREG;
            break;
        case VFS_DIRECTORY:
            mode |= S_IFDIR;
            break;
        case VFS_CHARDEVICE:
            mode |= S_IFCHR;
            break;
        case VFS_BLOCKDEVICE:
            mode |= S_IFBLK;
            break;
        case VFS_SYMLINK:
            mode |= S_IFLNK;
            break;
        default:
            mode |= S_IFREG;
            break;
    }
    
    mode |= (perms & 07777);
    return mode;
}

long vfs_stat(struct vfs_node *node, struct stat *statbuf, bool follow_symlinks) {
    if (!node)
        return -ENOENT;
    
    if (node->type == VFS_SYMLINK && follow_symlinks) {
        struct vfs_node *target = vfs_resolve_symlink(node, MAX_NESTED_SYMLINKS);
        if (!target)
            return -ENOENT;
        node = target;
    }
    
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_mode = convert_mode(node->type, node->perms);
    statbuf->st_nlink = 1;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_ino = node->inode;
    /** TODO: report block usage (st_blocks) */
    
    switch (node->type) {
        case VFS_FILE:
            statbuf->st_size = node->size;
            break;
        case VFS_DIRECTORY:
            statbuf->st_size = 4096;
            break;
        case VFS_SYMLINK:
            statbuf->st_size = node->symlink ? strlen(node->symlink->name /**/) : 0; /** TODO: check this */
            break;
        default:
            statbuf->st_size = 0;
            break;
    }
    return 0;
}

void vfs_install(void) {
    vfs_root = vfs_create_node("", VFS_DIRECTORY);
    
    devfs_initialize();
    zero_initialize();
    ps2_initialize();
    serial_initialize();
    tmpfs_initialize();
    tty_initialize();
    fbdev_initialize();
    procfs_initialize();

    dprintf("%s:%d: initialized VFS\n", __FILE__, __LINE__);
    //printf("\033[92m * \033[97mInitialized virtual filesystem\033[0m\n");
}