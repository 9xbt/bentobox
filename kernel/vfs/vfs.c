#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>
#include <kernel/tmpfs.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/spinlock.h>

extern void zero_initialize(void);
extern void ps2_initialize(void);
extern void serial_initialize(void);
extern void console_initialize(void);
extern void tmpfs_initialize(void);
extern void tty_initialize(void);
extern void fbdev_initialize(void);

struct vfs_node *vfs_root = NULL;
struct vfs_node *vfs_dev = NULL;

const char *vfs_types[] = {
    "VFS_NONE",
    "VFS_FILE",
    "VFS_DIRECTORY",
    "VFS_CHARDEVICE",
    "VFS_BLOCKDEVICE",
    "VFS_SYMLINK"
};

struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type) {
    struct vfs_node *node = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
    strcpy(node->name, name);
    node->busy = false;
    node->type = type;
    node->size = 0;
    node->perms = type == VFS_DIRECTORY ? 0755 : 0644;
    node->inode = 0;
    node->parent = NULL;
    node->children = NULL;
    node->next = NULL;
    node->read = NULL;
    node->write = NULL;
    node->symlink_target = NULL;
    node->isatty = false;
    node->ioctl = NULL;
    node->mmap = NULL;
    node->driver = VFS_DRIVER_OTHER;
    node->create = NULL;
    node->remove = NULL;
    return node;
}

void vfs_add_node(struct vfs_node *root, struct vfs_node *node) {
    // TODO: make node inherit the driver of root?
    if (!root) root = vfs_root;

    node->parent = root;
    if (root->children == NULL) {
        root->children = node;
    } else {
        struct vfs_node *child = root->children;
        while (child->next != NULL) {
            child = child->next;
        }
        child->next = node;
    }
}

int vfs_remove_node(struct vfs_node *node) {
    if (!node)
        return -EINVAL;
    if (node == vfs_root)
        return -EBUSY;
    if (node->busy)
        return -EBUSY;
    if (node->type == VFS_DIRECTORY && node->children != NULL)
        return -ENOTEMPTY;
    
    if (node->parent) {
        uint8_t parent_perms = (node->parent->perms >> 6) & 0x7;
        if (!(parent_perms & 0x2)) {
            return -EACCES;
        }
    }

    struct vfs_node *dir = node->parent;
    while (dir->parent != vfs_root) {
        dir = dir->parent;
    }

    switch (dir->driver) {
        case VFS_DRIVER_TMPFS:
            if (node->type == VFS_DIRECTORY) break;
            if (tmpfs_remove_file(node) == -EINVAL) {
                return -EINVAL;
            }
            break;
        case VFS_DRIVER_EXT2:
            return -EROFS;
        default:
            break;
    }
    
    if (node->parent) {
        if (node->parent->children == node) {
            node->parent->children = node->next;
        } else {
            struct vfs_node *prev = node->parent->children;
            while (prev && prev->next != node) {
                prev = prev->next;
            }
            if (prev) {
                prev->next = node->next;
            }
        }
    }
    
    if (node->type == VFS_SYMLINK && node->symlink_target) {
        kfree(node->symlink_target);
        node->symlink_target = NULL;
    }
    
    node->parent = NULL;
    node->children = NULL;
    node->next = NULL;
    node->read = NULL;
    node->write = NULL;
    
    kfree(node);
    return 0;
}

void vfs_add_device(struct vfs_node *node) {
    vfs_add_node(vfs_dev, node);
}

struct vfs_node *vfs_create_symlink(const char *name, const char *target) {
    struct vfs_node *node = vfs_create_node(name, VFS_SYMLINK);
    if (node && target) {
        node->symlink_target = kmalloc(strlen(target) + 1);
        strcpy(node->symlink_target, target);
        node->size = strlen(target);
    }
    return node;
}

struct vfs_node *vfs_resolve_symlink(struct vfs_node *symlink, int max_depth) {
    if (!symlink || symlink->type != VFS_SYMLINK || max_depth <= 0) {
        return symlink;
    }
    if (!symlink->symlink_target) {
        return NULL;
    }
    
    struct vfs_node *target;
    if (symlink->symlink_target[0] == '/') {
        target = vfs_open(vfs_root, symlink->symlink_target, false, false);
    } else {
        target = vfs_open(symlink->parent, symlink->symlink_target, false, false);
    }
    
    if (!target) {
        dprintf("%s:%d: target %s not found\n", __FILE__, __LINE__, symlink->symlink_target);
        return NULL;
    }
    
    if (target->type == VFS_SYMLINK) {
        return vfs_resolve_symlink(target, max_depth - 1);
    }
    return target;
}

struct vfs_node* vfs_open(struct vfs_node *current, const char *path, bool create, bool isdir) {
    // TODO: make this support returning actual error codes
    if (!path) return NULL;
    if (path[0] == '/' || !current) current = vfs_root;

    if (!strcmp(path, ".")) {
        return current;
    }
    if (!strcmp(path, "..")) {
        return current->parent;
    }

    const char *filename = path;
    for (int i = strlen(path) - 1; i >= 0; i--) {
        if (path[i] == '/') {
            filename = &path[i + 1];
            break;
        }
    }

    char *copy = kmalloc(strlen(path) + 1);
    strcpy(copy, path);
    char *token = strtok(copy, "/");

    struct vfs_node *node = current;
    while (token != NULL) {
        if (!strcmp(token, ".")) {
            /* do nothing */
        } else if (!strcmp(token, "..")) {
            if (node->parent) {
                node = node->parent;
            }
        } else {
            struct vfs_node *child = node->children;
            bool found = false;

            while (child != NULL) {
                if (strcmp(child->name, token) == 0) {
                    node = child;

                    if (node->type == VFS_SYMLINK) {
                        node = vfs_resolve_symlink(node, MAX_NESTED_SYMLINKS);
                        if (!node) {
                            kfree(copy);
                            return NULL;
                        }
                    }

                    found = true;
                    break;
                }
                child = child->next;
            }

            if (!found) {
                kfree(copy);

                if (create) {
                    switch (node->driver) {
                        case VFS_DRIVER_TMPFS:
                            if (isdir) break;
                            return tmpfs_create_file(node, filename);
                        default:
                            return NULL;
                    }
                    struct vfs_node *dir = vfs_create_node(filename, VFS_DIRECTORY);
                    dir->driver = node->driver;
                    vfs_add_node(node, dir);
                    return dir;
                }
                return NULL;
            }
        }
        token = strtok(NULL, "/");
    }

    kfree(copy);
    return node;
}

int vfs_close(struct vfs_node *node) {
    if (node->busy)
        return -EBUSY;
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
    if (!node) return -ENOENT;
    if (node->busy) return -EBUSY;
    if (node->read) {
        long ret = node->read(node, buffer, offset, len);
        return ret;
    }
    return -EINVAL;
}

long vfs_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node) return -ENOENT;
    if (node->busy) return -EBUSY;
    if (node->write) {
        long ret = node->write(node, buffer, offset, len);
        return ret;
    }
    return -EINVAL;
}

bool vfs_poll(struct vfs_node *node) {
    while (node->busy) {
        asm ("pause");
    }
    return true;
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
    statbuf->st_nlink = 0;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    if (symlink) {
        // TODO do this properly
        statbuf->st_nlink = 1;
        statbuf->st_ino = node->inode;
    }
    
    switch (node->type) {
        case VFS_FILE:
            statbuf->st_size = node->size;
            break;
        case VFS_DIRECTORY:
            statbuf->st_size = 4096;
            break;
        default:
            break;
    }
    return 0;
}

void vfs_install(void) {
    vfs_root = (struct vfs_node *)kmalloc(sizeof(struct vfs_node));
    vfs_root->type = VFS_DIRECTORY;
    vfs_root->size = 0;
    vfs_root->perms = 0;
    vfs_root->inode = 2;
    vfs_root->parent = NULL;
    vfs_root->children = NULL;
    vfs_root->next = NULL;
    vfs_root->read = NULL;
    vfs_root->write = NULL;
    vfs_root->symlink_target = NULL;
    vfs_root->isatty = false;
    vfs_root->driver = VFS_DRIVER_OTHER;

    vfs_dev = vfs_create_node("dev", VFS_DIRECTORY);
    vfs_dev->driver = VFS_DRIVER_DEVFS;
    vfs_add_node(vfs_root, vfs_dev);

    zero_initialize();
    ps2_initialize();
    serial_initialize();
    console_initialize();
    tmpfs_initialize();
    tty_initialize();
    fbdev_initialize();

    dprintf("%s:%d: initialized VFS\n", __FILE__, __LINE__);
    //printf("\033[92m * \033[97mInitialized virtual filesystem\033[0m\n");
}