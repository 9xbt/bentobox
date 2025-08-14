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
#include <kernel/time.h>
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

struct vfs_node *vfs_root = NULL, *vfs_devfs = NULL;
struct vfs_mountpoint vfs_mounts[MAX_MOUNTS] = {0};

struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type) {
    struct vfs_node *node = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
    strcpy(node->name, name);
    node->busy = false;
    node->isatty = false;
    node->type = type;
    node->size = 0;
    node->blocks = 0;
    node->perms = type == VFS_DIRECTORY ? 0755 : 0644;
    node->inode = 0;
    node->parent = NULL;
    node->symlink = NULL;
    node->children = list_create();
    node->poll_list = list_create();
    node->tty_ops.ioctl = NULL;
    node->tty_ops.enqueue = NULL;
    node->tty_ops.dequeue = NULL;
    node->tty_ops.flush = NULL;
    node->open = NULL;
    node->create = NULL;
    node->remove = NULL;
    node->mkdir = NULL;
    node->read = NULL;
    node->write = NULL;
    node->mmap = NULL;
    node->poll = NULL;
    node->close = NULL;
    node->device = NULL;
    node->a_time = now();
    node->c_time = now();
    node->m_time = now();
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

    long ret = node->remove ? node->remove(node) : -EPERM;
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

static struct vfs_node *vfs_find_child(struct vfs_node *parent, const char *name, bool follow) {
    foreach(item, parent->children) {
        struct vfs_node *child = item->value;
        if (!strcmp(child->name, name)) {
            return (child->type == VFS_SYMLINK && follow) ? vfs_resolve_symlink(child, MAX_NESTED_SYMLINKS) : child;
        }
    }
    return NULL;
}

static struct vfs_node *vfs_touch(struct vfs_node *parent, const char *name, bool isdir) {
    return isdir
        ? parent->mkdir ? parent->mkdir(parent, name) : NULL
        : (parent->create ? parent->create(parent, name) : NULL);
}

struct vfs_node* vfs_open(struct vfs_node *current, const char *path, bool create, bool isdir_follow) {
    if (!path) return NULL;
    if (path[0] == '/' || !current) current = vfs_root;

    if (!strcmp(path, ".")) return current;
    if (!strcmp(path, "..")) return current->parent ?: current;

    char *copy = kmalloc(strlen(path) + 1);
    strcpy(copy, path);

    struct vfs_node *node = current;
    char *token = strtok(copy, "/");
    char *next = strtok(NULL, "/");

    while (token) {
        if (!strcmp(token, ".")) {
            // skip
        } else if (!strcmp(token, "..")) {
            node = node->parent ?: node;
        } else {
            struct vfs_node *child = vfs_find_child(node, token, !isdir_follow);
            
            if (!child) {
                if (create && !next) {
                    child = vfs_touch(node, token, isdir_follow);
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
        }

        token = next;
        next = strtok(NULL, "/");
    }

    kfree(copy);
    return node;
}

int vfs_close(struct vfs_node *node) {
    if (!node)
        return -ENOENT;
    if (node->busy)
        return -EBUSY;
    if (node->close)
        return node->close(node);
    return 0;
}

void vfs_resolve_path(char *s, struct vfs_node *node) {
    if (!node) node = vfs_root;
    if (node == vfs_root) {
        strcpy(s, "/");
        return;
    }

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
    node->a_time = now();
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
    node->a_time = now();
    node->m_time = now();
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

long vfs_stat(struct vfs_node *node, struct stat *statbuf, bool symlink) {
    if (!node)
        return -ENOENT;

    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_mode = convert_mode(node->type, node->perms);
    statbuf->st_nlink = 1;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_ino = node->inode;
    statbuf->st_atim.tv_sec = node->a_time;
    statbuf->st_ctim.tv_sec = node->c_time;
    statbuf->st_mtim.tv_sec = node->m_time;
    
    switch (node->type) {
        case VFS_FILE:
        case VFS_DIRECTORY:
            statbuf->st_size = node->size;
            statbuf->st_blocks = node->blocks;
            break;
        case VFS_SYMLINK:
            statbuf->st_size = node->symlink ? (symlink ? strlen(node->symlink->name) : node->symlink->size) : 0;
            break;
        default:
            statbuf->st_size = 0;
            break;
    }
    return 0;
}

void vfs_register(const char *name, vfs_mount_callback mount) {
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!vfs_mounts[i].name) {
            struct vfs_mountpoint *mp = &vfs_mounts[i];
            mp->mount = mount;
            mp->name = kmalloc(strlen(name) + 1);
            strcpy(mp->name, name);

            dprintf(LOG_INFO, "%s:%d: registered mount '%s'\n", __FILE__, __LINE__, name);
            return;
        }
    }
}

long vfs_mount(struct vfs_node *source, struct vfs_node *target, const char *fstype, unsigned long flags) {
    if (!target) 
        return -ENOENT;
    if (!fstype)
        return -EFAULT;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (vfs_mounts[i].name && !strcmp(vfs_mounts[i].name, fstype)) {
            return vfs_mounts[i].mount(source, target);
        }
    }
    return -EINVAL;
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

    dprintf(LOG_INFO, "%s:%d: initialized VFS\n", __FILE__, __LINE__);
    //printf("\033[92m * \033[97mInitialized virtual filesystem\033[0m\n");
}