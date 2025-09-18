#include <stdbool.h>
#include <stddef.h>
#include <kernel/lfbvideo.h>
#include <kernel/vfs.h>

long console_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    flanterm_write(ft_ctx, buffer, len);
    return len;
}

vfs_ops_t console_ops = {
    .write = console_write
};

void console_initialize(void) {
    vfs_node_t *console = vfs_create_node("console", VFS_CHARDEVICE);
    console->ops = &console_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), console);
}