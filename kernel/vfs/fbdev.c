#include <kernel/lfbvideo.h>
#include <kernel/termios.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#define FLANTERM_IN_FLANTERM
#include <flanterm_private.h>

long fbdev_mmap(vfs_node_t *node, void *addr, size_t pages, uint64_t prot, int flags, long offset) {
    (void)node;
    (void)offset;
    // dprintf(LOG_DEBUG, "\033[93m%s:\033[0m offset is ignored\n", __func__);

    return (long)vmalloc(this_proc->vma, this_proc->pm, (flags & MAP_FIXED) ? (uintptr_t)addr : 0, (uintptr_t)PHYSICAL_HHDM(framebuffer->address), pages, prot);
}

long fbdev_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    size_t count = len < node->size - offset ? len : node->size - offset;
    memcpy((void *)(framebuffer->address + (uintptr_t)offset), buffer, count);
    return count;
}

long fbdev_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    switch (op) {
        case FBIOGET_VSCREENINFO: {
            struct fb_var_screeninfo vinfo = {0};
            framebuffer_get_vinfo(&vinfo);
            return copy_to_user(arg, &vinfo, sizeof(struct fb_var_screeninfo));
        }
        case FBIOGET_FSCREENINFO: {
            struct fb_fix_screeninfo finfo = {0};
            framebuffer_get_finfo(&finfo);
            return copy_to_user(arg, &finfo, sizeof(struct fb_var_screeninfo));
        }
        case FBIOPUT_VSCREENINFO:
        case FBIOPAN_DISPLAY:
        case FBIOPUTCMAP:
        case FBIOBLANK:
            return 0;
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
    return 0;
}

long fbdev_close(vfs_node_t *node) {
    (void)node;
    // ft_ctx->full_refresh(ft_ctx);
    return 0;
}

vfs_ops_t fbdev_ops = {
    .close = fbdev_close,
    .write = fbdev_write,
    .mmap = fbdev_mmap
};

vfs_tty_ops_t fbdev_tty_ops = {
    .ioctl = fbdev_ioctl
};

void fbdev_initialize(void) {
    if (!framebuffer)
        return;

    vfs_node_t *fb0 = devfs_create_numbered(DEVFS_FB);
    fb0->size = framebuffer->pitch * framebuffer->height;
    fb0->perms = 0660;
    fb0->ops = &fbdev_ops;
    fb0->tty_ops = &fbdev_tty_ops;
}