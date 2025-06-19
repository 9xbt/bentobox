#include <stdint.h>
#include <stddef.h>
#include <kernel/vfs.h>

void tty_initialize(void) {
    vfs_node_t *stdin = vfs_open(NULL, "/dev/keyboard", false);
    vfs_node_t *stdout = vfs_open(NULL, "/dev/console", false);

    vfs_node_t *tty = vfs_create_node("tty", VFS_CHARDEVICE);
    tty->read = stdin->read;
    tty->write = stdout->write;
    tty->isatty = true;
    vfs_add_device(tty);
}