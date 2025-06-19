#include <stdint.h>
#include <stddef.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>

long console_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *buf = (char *)buffer;
    for (uint32_t i = 0; i < len; i++) {
        putchar(buf[i]);
    }
    return (int32_t)len;
}

void console_initialize(void) {
    struct vfs_node *console = vfs_create_node("console", VFS_CHARDEVICE);
    console->write = console_write;
    console->isatty = true;
    vfs_add_device(console);
}