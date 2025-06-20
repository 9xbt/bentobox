#include <errno.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/printf.h>
#include <kernel/lfbvideo.h>

long fbdev_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (addr == NULL) {
        return framebuffer.addr + offset;
    }
    return -EINVAL;
}

void fbdev_initialize(void) {
    struct vfs_node *fb0 = vfs_create_node("fb0", VFS_CHARDEVICE);
    fb0->mmap = fbdev_mmap;
    vfs_add_device(fb0);
}