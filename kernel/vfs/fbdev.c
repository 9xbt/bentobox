#include <errno.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/ioctl.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/lfbvideo.h>

struct fb_var_screeninfo vinfo = {
    /*
    .xres = 1280,
    .yres = 800,
    .xres_virtual = 1280,
    .yres_virtual = 800,
    .bits_per_pixel = 32,
    .transp = { .offset = 24, .length = 8, .msb_right = 0 },
    .red    = { .offset = 16, .length = 8, .msb_right = 0 },
    .green  = { .offset = 8,  .length = 8, .msb_right = 0 },
    .blue   = { .offset = 0,  .length = 8, .msb_right = 0 },
    */
};

long fbdev_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (addr == NULL) {
        return framebuffer.addr + offset;
    }
    return -EINVAL;
}

long fbdev_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    memcpy((void *)(framebuffer.addr + (uintptr_t)offset), buffer, len);
    dprintf("fbdev write! %lu bytes\n", len);
    return len;
}

long fbdev_ioctl(int fd_num, int op, void *arg) {
    //struct fd *fd = &this->fd_table[fd_num];
    switch (op) {
        case FBIOGET_VSCREENINFO:
            memcpy(arg, &vinfo, sizeof vinfo);
            return 0;
        default:
            dprintf("%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
    return 0;
}

void fbdev_initialize(void) {
    struct vfs_node *fb0 = vfs_create_node("fb0", VFS_CHARDEVICE);
    fb0->write = fbdev_write;
    fb0->mmap = fbdev_mmap;
    fb0->isatty = true;
    fb0->ioctl = fbdev_ioctl;
    vfs_add_device(fb0);
}