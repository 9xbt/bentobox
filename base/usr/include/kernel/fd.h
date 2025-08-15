#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <kernel/vfs.h>
#include <sys/termios.h>

#define stdin  0
#define stdout 1
#define stderr 2

struct fd {
    struct vfs_node *node;
    bool open;
    int flags;
    size_t offset;
    struct termios tio;
};

struct fd fd_new(struct vfs_node *node, int flags);
int fd_create(struct vfs_node *node, int flags);
int fd_open(const char *path, int flags);
int fd_close(int fd);
int fd_dup(int oldfd_num, int newfd_num);
struct fd *fd_get(int fd);
struct fd *fd_get_from_node(struct vfs_node *node);