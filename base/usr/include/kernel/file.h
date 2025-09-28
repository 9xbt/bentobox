#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <kernel/termios.h>
#include <kernel/vfs.h>

typedef struct file {
    vfs_node_t *node;
    bool open;
    int flags;
    size_t offset;
    struct termios tio;
} file_t;

struct file file_new(struct vfs_node *node, int flags);
int file_create(struct vfs_node *node, int flags);
int file_open(const char *path, int flags);
int file_close(int fd);
int file_dup(int oldfd, int newfd, int flags);
struct file *file_get(int fd);
struct file *file_get_from_node(struct vfs_node *node);