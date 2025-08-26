#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/list.h>

#define MAX_PATH 256

typedef enum vfs_node_type {
    VFS_NONE,
    VFS_FILE,
    VFS_DIRECTORY,
    VFS_CHARDEVICE,
    VFS_BLOCKDEVICE,
    VFS_SYMLINK,
    VFS_UNIXPIPE,
    VFS_SOCKET
} vfs_node_type_t;

struct vfs_node_ops {
    struct vfs_node *(*open)(struct vfs_node *node, int flags);
    long (*close)(struct vfs_node *node);
    long (*read)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long (*write)(struct vfs_node *node, const void *buffer, long offset, size_t len);
    struct vfs_node *(*create)(struct vfs_node *parent, const char *name, enum vfs_node_type type);
    long (*remove)(struct vfs_node *node);
    long (*rename)(struct vfs_node *node, const char *name);
    long (*mmap)(struct vfs_node *node, void *addr, size_t length, int prot, int flags, long offset);
    long (*poll)(struct vfs_node *node, long events);
};

typedef struct vfs_node {
    char name[MAX_PATH];
    bool open;
    enum vfs_node_type type;
    size_t size;
    size_t blocks;
    uint16_t perms;
    uint64_t inode;
    uint64_t flags;
    uint64_t atime;
    uint64_t ctime;
    uint64_t mtime;
    list_t *children;
    struct vfs_node *parent;
    struct vfs_node *symlink;
    struct vfs_node_ops *ops;
    void *device;
} vfs_node_t;

void vfs_install(void);
struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type);
struct vfs_node *vfs_add_node(struct vfs_node *parent, struct vfs_node *node);
long vfs_remove_node(struct vfs_node *node);
struct vfs_node *vfs_find_child(struct vfs_node *parent, const char *name, bool follow);
struct vfs_node *vfs_lookup(struct vfs_node *cwd, const char *path, bool follow_symlinks);
struct vfs_node *vfs_open(struct vfs_node *cwd, const char *path, long flags);
long vfs_close(struct vfs_node *node);
long vfs_read(struct vfs_node *node, void *buffer, long offset, size_t len);
long vfs_write(struct vfs_node *node, void *buffer, long offset, size_t len);