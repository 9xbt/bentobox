#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/list.h>
#include <kernel/time.h>

#define MAX_PATH    256

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

#define S_IFMT      0xF000
#define S_IFIFO     0x1000
#define S_IFCHR     0x2000
#define S_IFDIR     0x4000
#define S_IFBLK     0x6000
#define S_IFREG     0x8000
#define S_IFLNK     0xA000
#define S_IFSOCK    0xC000
#define S_IFWHT     0xE000

#define AT_FDCWD            -100
#define AT_SYMLINK_NOFOLLOW 0x100

#define POLLIN      0x001
#define POLLOUT     0x004
#define POLLNVAL    0x020

struct stat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;

	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	unsigned int __pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;

	struct timespec st_atim;
	struct timespec st_mtim;
	struct timespec st_ctim;
	long __unused[3];
};

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

typedef struct vfs_ops {
    struct vfs_node *(*open)(struct vfs_node *node, int flags);
    long(*close)(struct vfs_node *node);
    long(*read)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long(*write)(struct vfs_node *node, const void *buffer, long offset, size_t len);
    struct vfs_node *(*create)(struct vfs_node *parent, const char *name, enum vfs_node_type type);
    long(*remove)(struct vfs_node *node);
    long(*rename)(struct vfs_node *node, const char *name);
    long(*mmap)(struct vfs_node *node, void *addr, size_t length, int prot, int flags, long offset);
    long(*poll)(struct vfs_node *node, long events);
} vfs_ops_t;

typedef struct vfs_tty_ops {
    long(*ioctl)(int fd, int op, void *arg);
    long(*enqueue)(int c);
    long(*dequeue)(bool block);
    void(*flush)(void);
} vfs_tty_ops_t;

typedef struct vfs_node {
    char name[MAX_PATH];
    bool busy;
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
    list_t *waiters;
    struct vfs_node *parent;
    struct vfs_node *symlink;
    struct vfs_ops *ops;
    struct vfs_tty_ops *tty_ops;
    void *device;
} vfs_node_t;

void vfs_install(void);
vfs_node_t *vfs_get_root(void);
vfs_node_t *vfs_create_node(const char *name, enum vfs_node_type type);
vfs_node_t *vfs_add_node(vfs_node_t *parent, vfs_node_t *node);
long vfs_remove_node(vfs_node_t *node);
vfs_node_t *vfs_find_child(vfs_node_t *parent, const char *name, bool follow);
vfs_node_t *vfs_lookup(vfs_node_t *cwd, const char *path, bool follow_symlinks, enum vfs_node_type create_type);
vfs_node_t *vfs_open(vfs_node_t *cwd, const char *path, long flags);
long vfs_close(vfs_node_t *node);
long vfs_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long vfs_write(vfs_node_t *node, void *buffer, long offset, size_t len);
long vfs_poll(vfs_node_t *node, long events, long timeout);
void vfs_wake_waiters(vfs_node_t *node);
char *vfs_resolve_path(vfs_node_t *node);
void vfs_print_tree(vfs_node_t *node);