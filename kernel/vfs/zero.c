#include <stdint.h>
#include <stddef.h>
#include <kernel/vfs.h>
#include <kernel/string.h>

long zero_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    memset(buffer, 0, len);
    return (int32_t)len;
}

long null_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    return 0;
}

long null_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    return len;
}

void zero_initialize(void) {
    struct vfs_node *zero = vfs_create_node("zero", VFS_CHARDEVICE);
    zero->read = zero_read;
    vfs_add_device(zero);

    struct vfs_node *null = vfs_create_node("null", VFS_CHARDEVICE);
    null->read = null_read;
    null->write = null_write;
    vfs_add_device(null);
}