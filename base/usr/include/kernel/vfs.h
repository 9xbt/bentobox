#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <kernel/list.h>

#define MAX_PATH            256
#define MAX_NESTED_SYMLINKS 10
#define MAX_MOUNTS          16

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
    size_t size;
    size_t blocks;
    uint16_t perms;
    uint64_t inode;
    struct vfs_node *parent;
    struct vfs_node *symlink;
    list_t *children;
    list_t *poll_list;
    struct tty_operations tty_ops;
    long(*read)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long(*write)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long(*mmap)(struct vfs_node *node, void *addr, size_t length, int prot, int flags, off_t offset);
    long(*poll)(struct vfs_node *node, long events);
    long(*close)(struct vfs_node *node);
    long(*remove)(struct vfs_node *node);
    struct vfs_node *(*create)(struct vfs_node *parent, const char *name);
    struct vfs_node *(*mkdir)(struct vfs_node *parent, const char *name);
    void *device;
    uint64_t a_time;
    uint64_t c_time;
    uint64_t m_time;
} vfs_node_t;

typedef long (*vfs_mount_callback)(struct vfs_node *source, struct vfs_node *target);

typedef struct vfs_mountpoint {
    char *name;
    vfs_mount_callback mount;
} vfs_mountpoint_t;

extern struct vfs_node *vfs_root, *vfs_devfs;

void vfs_install(void);
void vfs_add_node(struct vfs_node *parent, struct vfs_node *node);
void vfs_add_device(struct vfs_node *node);
void vfs_resolve_path(char *s, struct vfs_node *node);
long vfs_read(struct vfs_node *node, void *buffer, long offset, size_t len);
long vfs_write(struct vfs_node *node, void *buffer, long offset, size_t len);
struct vfs_node *vfs_create_node(const char *name, enum vfs_node_type type);
struct vfs_node* vfs_open(struct vfs_node *current, const char *path, bool create, bool isdir_follow);
int vfs_close(struct vfs_node *node);
struct vfs_node *vfs_create_symlink(const char *name, const char *target);
struct vfs_node *vfs_resolve_symlink(struct vfs_node *symlink, int max_depth);
long vfs_poll(struct vfs_node *node, long events, long timeout);
void vfs_unblock_polling(struct vfs_node *node);
int vfs_remove_node(struct vfs_node *node);
long vfs_check_perms(struct vfs_node *node, int mode);
long vfs_stat(struct vfs_node *node, struct stat *statbuf, bool symlink);
void vfs_register(const char *name, vfs_mount_callback mount);
long vfs_mount(struct vfs_node *source, struct vfs_node *target, const char *fstype, unsigned long flags);