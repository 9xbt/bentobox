#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/lfb.h>
#include <kernel/vfs.h>
#include <kernel/ioctl.h>
#include <kernel/printf.h>
#include <kernel/string.h>

long console_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *buf = (char *)buffer;
    for (uint32_t i = 0; i < len; i++) {
        putchar(buf[i]);
    }
    return (int32_t)len;
}

long console_ioctl(int fd_num, int op, void *arg) {
    struct fd *fd = &this->fd_table[fd_num];
    switch (op) {
        case TCGETS:
            memcpy(arg, &fd->tio, sizeof(struct termios));
            return 0;
        case TCSETS:
        case TCSETSW:
            memcpy(&fd->tio, arg, sizeof(struct termios));
            return 0;
        case TIOCGWINSZ:
            lfb_get_ws((struct winsize *)arg);
            return 0;
        case TIOCGNAME:
            strcpy(arg, "/dev/console");
            return 0;
        case KDFONTOP: {
            struct console_font_op *fop = (struct console_font_op *)arg;
            switch (fop->op) {
                case KD_FONT_OP_SET: {
                    unsigned int vpitch = 32;
                    unsigned int bpc = fop->height;
                    
                    vfs_node_t *file = vfs_open(NULL, "/tmp/font", true);
                    size_t off = 0;
                    for (unsigned int i = 0; i < fop->charcount; i++) {
                        vfs_write(file, (void *)fop->data + (i * vpitch), off, bpc);
                        off += bpc;
                    }
                    
                    vfs_close(file);
                    lfb_change_font("/tmp/font");
                    vfs_remove_node(file);
                    return 0;
                }
                default:
                    return -EINVAL;
            }
        }
        case PIO_UNIMAP:
            return 0;
        case PIO_UNIMAPCLR:
            return 0;
        default:
            dprintf("%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
}

void console_initialize(void) {
    struct vfs_node *console = vfs_create_node("console", VFS_CHARDEVICE);
    console->write = console_write;
    console->isatty = true;
    console->ioctl = console_ioctl;
    vfs_add_device(console);
}