#include <stddef.h>
#include <stdint.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

long zero_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    memset(buffer, 0, len);
    return (int32_t)len;
}

long null_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    (void)buffer;
    (void)offset;
    (void)len;
    return 0;
}

long null_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    (void)buffer;
    (void)offset;
    return len;
}

vfs_ops_t zero_ops = {
    .read = zero_read
};

vfs_ops_t null_ops = {
    .read = null_read,
    .write = null_write
};

void zero_initialize(void) {
    vfs_node_t *zero = vfs_create_node("zero", VFS_CHARDEVICE);
    zero->perms = 0666;
    zero->ops = &zero_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), zero);

    vfs_node_t *null = vfs_create_node("null", VFS_CHARDEVICE);
    null->perms = 0666;
    null->ops = &null_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), null);
}