#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>
#include <kernel/time.h>

#define MAX_PATH        256
#define MAX_SYMLINKS    40

#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_CREAT		00000100
#define O_TRUNC		00001000
#define O_APPEND	00002000
#define O_NONBLOCK	00004000
#define O_CLOEXEC	02000000

#ifdef __x86_64__
#define O_DIRECT      040000
#define O_LARGEFILE  0100000
#define O_DIRECTORY  0200000
#define O_NOFOLLOW   0400000
#elif __aarch64__
#define O_DIRECTORY   040000
#define O_NOFOLLOW   0100000
#define O_DIRECT     0200000
#define O_LARGEFILE  0400000
#endif

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
#define AT_REMOVEDIR        0x200
#define AT_SYMLINK_FOLLOW   0x400
#define AT_EMPTY_PATH       0x1000

#define POLLIN      0x001
#define POLLOUT     0x004
#define POLLHUP		0x010
#define POLLNVAL    0x020

#define S_IRUSR 0x100
#define S_IWUSR 0x80
#define S_IXUSR 0x40

#define S_IRGRP 0x20
#define S_IWGRP 0x10
#define S_IXGRP 0x8

#define S_IROTH 0x4
#define S_IWOTH 0x2
#define S_IXOTH 0x1

#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1

#ifdef __x86_64__
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
#elif __aarch64__
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t st_size;
    int64_t st_blksize;
    int32_t __pad2;
    int64_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    int32_t __pad3[2];
};
#endif

typedef enum vfs_node_type {
    VFS_NONE,
    VFS_FILE,
    VFS_DIRECTORY,
    VFS_CHARDEVICE,
    VFS_BLOCKDEVICE,
    VFS_SYMLINK,
    VFS_UNIXPIPE,
    VFS_SOCKET,
    VFS_PTY
} vfs_node_type_t;

#define DEVFS_BITMAP_SIZE 32
#define DEVFS_INODE_BASE  1000000

typedef enum devfs_type {
    DEVFS_EVENT,
    DEVFS_FB,
    DEVFS_SSD,
    DEVFS_PTY,
    DEVFS_TTY,
    DEVFS_STTY,
    DEVFS_MAX
} devfs_type_t;

struct vfs_node;
struct vfs_mountpoint;
struct vfs_result;

typedef struct vfs_ops {
    struct vfs_result(*open)(struct vfs_node *node, int flags);
    long(*close)(struct vfs_node *node);
    long(*read)(struct vfs_node *node, void *buffer, long offset, size_t len);
    long(*write)(struct vfs_node *node, const void *buffer, long offset, size_t len);
    struct vfs_result(*create)(struct vfs_node *parent, const char *name, enum vfs_node_type type);
    long(*remove)(struct vfs_node *node);
    long(*rename)(struct vfs_node *node, struct vfs_node *parent, const char *name);
    long(*mmap)(struct vfs_node *node, void *addr, size_t pages, uint64_t prot, int flags, long offset);
    long(*poll)(struct vfs_node *node, long events);
    long(*chmod)(struct vfs_node *node, unsigned int mode);
    long(*link)(struct vfs_node *old_node, struct vfs_node *new_node);
    long(*ioctl)(struct vfs_node *node, int op, void *arg);
} vfs_ops_t;

typedef struct vfs_node {
    char name[MAX_PATH];
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
    spinlock_t waiters_lock;
    struct vfs_node *parent;
    struct vfs_node *symlink;
    struct vfs_ops *ops;
    void *device;
    char *target;
    struct vfs_mountpoint *mount;
    int refcount;
} vfs_node_t;

typedef struct vfs_mount_ops {
    const char *type;
    bool nodev;
    long(*mount)(struct vfs_node *node, struct vfs_node *device, long flags);
    long(*unmount)(struct vfs_node *node, long flags);
} vfs_mount_ops_t;

typedef struct vfs_mountpoint {
    struct vfs_mount_ops *ops;
    struct vfs_node *node;
    struct vfs_node *device;
    long flags;
} vfs_mountpoint_t;

typedef struct vfs_result {
    struct vfs_node *node;
    long error;
} vfs_result_t;

void vfs_install(void);
vfs_node_t *vfs_get_root(void);
vfs_node_t *vfs_create_node(const char *name, enum vfs_node_type type);
vfs_node_t *vfs_create_symlink(const char *name, const char *target);
vfs_node_t *vfs_add_node(vfs_node_t *parent, vfs_node_t *node);
long vfs_remove_node(vfs_node_t *node);
long vfs_remove(vfs_node_t *node);
long vfs_rename(vfs_node_t *node, vfs_node_t *parent, const char *path);
vfs_result_t vfs_resolve_symlink(vfs_node_t *node, int depth);
vfs_result_t vfs_find_child(vfs_node_t *parent, const char *name, bool follow);
vfs_result_t vfs_touch(vfs_node_t *parent, const char *name, enum vfs_node_type type);
vfs_result_t vfs_lookup(vfs_node_t *cwd, const char *path, bool follow_symlinks, enum vfs_node_type create_type);
vfs_result_t vfs_open(vfs_node_t *cwd, const char *path, long flags);
long vfs_close(vfs_node_t *node);
long vfs_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long vfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len);
long vfs_poll(vfs_node_t *node, long events, long timeout);
long vfs_poll_multiplexed(vfs_node_t **nodes, short *events, short *revents, long nfds, long timeout);
void vfs_wake_waiters(vfs_node_t *node);
void vfs_register(vfs_mount_ops_t *ops);
void vfs_unregister(vfs_mount_ops_t *ops);
long vfs_mount(vfs_node_t *node, const char *type, vfs_node_t *device, long flags);
long vfs_unmount(vfs_node_t *node, long flags);
long vfs_chmod(vfs_node_t *node, unsigned int mode);
long vfs_link(vfs_node_t *old_node, vfs_node_t *new_node);
char *vfs_resolve_path(vfs_node_t *node);
void vfs_print_tree(vfs_node_t *node);
vfs_node_t *devfs_create_node(const char *name, vfs_node_type_t type);
vfs_node_t *devfs_create_numbered(devfs_type_t type);
void devfs_remove_numbered(devfs_type_t type, vfs_node_t *node);