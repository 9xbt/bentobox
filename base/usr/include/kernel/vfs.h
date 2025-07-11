#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/stat.h>

#define MAX_PATH            256
#define MAX_NESTED_SYMLINKS 10

typedef enum vfs_node_type {
    VFS_NONE,
    VFS_FILE,
    VFS_DIRECTORY,
    VFS_CHARDEVICE,
    VFS_BLOCKDEVICE,
    VFS_SYMLINK,
    VFS_UNIXPIPE
} vfs_node_type_t;

typedef enum vfs_driver {
    VFS_DRIVER_OTHER,
    VFS_DRIVER_EXT2,
    VFS_DRIVER_TMPFS,
    VFS_DRIVER_DEVFS,
    
    VFS_MAX_DRIVERS
} vfs_driver_t;

typedef struct vfs_driver_ops {
    struct vfs_node *(*create)(struct vfs_node *parent, const char *name);
    long(*remove)(struct vfs_node *node);
    struct vfs_node *(*mkdir)(struct vfs_node *parent, const char *name);
    long(*rmdir)(struct vfs_node *node);
} vfs_driver_ops_t;

typedef struct tty_operations {
    long(*ioctl)(int fd, int op, void *arg);
    long(*enqueue)(int c);
    long(*dequeue)(bool block);
    void(*flush)();
} tty_ops_t;

typedef struct vfs_node {
    char name[MAX_PATH];
    bool busy, isatty;
    enum vfs_node_type type;
    enum vfs_driver driver;
    size_t size;
    uint16_t perms;
    uint64_t inode;
    struct vfs_node *parent;
    struct vfs_node *children; /** TODO: use a list here */
    struct vfs_node *next;
    struct tty_operations tty_ops;
    long(*read)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long(*write)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long(*mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    long(*poll)(struct vfs_node *node);
    long(*close)(struct vfs_node *node);
    void *device;
    struct vfs_node *symlink;
} vfs_node_t;

extern struct vfs_node *vfs_root;

void vfs_install(void);
void vfs_add_node(struct vfs_node *parent, struct vfs_node *node);
void vfs_add_device(struct vfs_node *node);
void vfs_resolve_path(char *s, struct vfs_node *node);
long vfs_read(struct vfs_node *node, void *buffer, long offset, size_t len);
long vfs_write(struct vfs_node *node, void *buffer, long offset, size_t len);
struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type);
struct vfs_node* vfs_open(struct vfs_node *current, const char *path, bool create, bool isdir);
int vfs_close(struct vfs_node *node);
struct vfs_node *vfs_create_symlink(const char *name, const char *target);
struct vfs_node *vfs_resolve_symlink(struct vfs_node *symlink, int max_depth);
bool vfs_poll(struct vfs_node *node);
int vfs_remove_node(struct vfs_node *node);
long vfs_check_perms(struct vfs_node *node, int mode);
long vfs_stat(struct vfs_node *node, struct stat *statbuf, bool symlink);