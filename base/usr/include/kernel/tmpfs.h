#pragma once
#include <stddef.h>
#include <kernel/vfs.h>

#define TMPFS_ROOT 999999
#define TMPFS_HASH_SIZE 256

long tmpfs_read(struct vfs_node *node, void *buffer, long offset, size_t len);
long tmpfs_write(struct vfs_node *node, void *buffer, long offset, size_t len);
struct vfs_node *tmpfs_create_file(struct vfs_node *parent, const char *name);
long tmpfs_remove_file(struct vfs_node *node);