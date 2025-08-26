#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/list.h>

#define MAX_PATH 256

#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_CREAT		00000100
#define O_TRUNC		00001000
#define O_APPEND	00002000
#define O_NONBLOCK	00004000
#define O_NOFOLLOW	00400000
#define O_CLOEXEC	02000000

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
    bool open, busy;
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
vfs_node_t *vfs_create_node(const char *name, enum vfs_node_type type);
vfs_node_t *vfs_add_node(vfs_node_t *parent, vfs_node_t *node);
long vfs_remove_node(vfs_node_t *node);
vfs_node_t *vfs_find_child(vfs_node_t *parent, const char *name, bool follow);
vfs_node_t *vfs_lookup(vfs_node_t *cwd, const char *path, bool follow_symlinks, enum vfs_node_type create_type);
vfs_node_t *vfs_open(vfs_node_t *cwd, const char *path, long flags);
long vfs_close(vfs_node_t *node);
long vfs_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long vfs_write(vfs_node_t *node, void *buffer, long offset, size_t len);