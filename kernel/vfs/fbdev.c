#include <kernel/lfbvideo.h>
#include <kernel/termios.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

long fbdev_mmap(vfs_node_t *node, void *addr, size_t pages, uint64_t prot, int flags, long offset) {
    (void)node;
    (void)offset;
    dprintf(LOG_DEBUG, "\033[93m%s:\033[0m offset is ignored\n", __func__);

    return (long)vmalloc(this_proc->vma, this_proc->pm, (flags & MAP_FIXED) ? (uintptr_t)addr : 0, (uintptr_t)PHYSICAL_HHDM(framebuffer->address), pages, prot);
}

long fbdev_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    memcpy((void *)(framebuffer->address + (uintptr_t)offset), buffer, len);
    return len;
}

long fbdev_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    switch (op) {
        case FBIOGET_VSCREENINFO: {
            struct fb_var_screeninfo vinfo;
            framebuffer_get_vinfo(&vinfo);
            return copy_to_user(arg, &vinfo, sizeof(struct fb_var_screeninfo));
        }
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
    return 0;
}

vfs_ops_t fbdev_ops = {
    .write = fbdev_write,
    .mmap = fbdev_mmap
};

vfs_tty_ops_t fbdev_tty_ops = {
    .ioctl = fbdev_ioctl
};

void fbdev_initialize(void) {
    vfs_node_t *fb0 = vfs_create_node("fb0", VFS_CHARDEVICE);
    fb0->perms = 0660;
    fb0->ops = &fbdev_ops;
    fb0->tty_ops = &fbdev_tty_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), fb0);
}