#include <stddef.h>
#include <kernel/spinlock.h>
#include <kernel/lfbvideo.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/fifo.h>
#include <kernel/list.h>
#include <kernel/file.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

static struct fifo *tty_fifo;
static int tty_group = 1;

extern long console_ioctl(int fd_num, int op, void *arg);
extern long ps2_ioctl(int fd_num, int op, void *arg);
extern long serial_ioctl(int fd_num, int op, void *arg);

void tty_flush(void) {
    int c;
    while (fifo_dequeue(tty_fifo, &c) > 0) {
        if (c > 0) putchar(c);
    }
}

long tty_enqueue(int c) {
    if (c <= 0)
        return 0;
    switch (c) {
        case 0x03:
            signal_send(sched_find_in_group(tty_group), SIGINT);
            puts("^C\n");
            break;
        case 0x1A:
            signal_send(sched_find_in_group(tty_group), SIGTSTP);
            puts("^Z\n");
            break;
        case 0x0C:
            puts("\033[H\033[J");
            break;
        case 0x1C:
            signal_send(this_proc, SIGQUIT);
            puts("^\\\n");
            break;
        default:
            return fifo_enqueue(tty_fifo, c);
    }
    return 0;
}

void tty_enqueue_string(char *str) {
    while (*str) {
        tty_enqueue(*str++);
    }
}

long tty_dequeue(bool block) {
    int c = 0;
    while (fifo_dequeue(tty_fifo, &c) > 0) {
        if (!block) {
            return -EAGAIN;
        }
    }
    return c;
}

long tty_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    if (!node->tty_ops)
        return -ENOTTY;

    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (node->tty_ops->enqueue(buf[i]) < 0)
            break;
    }
    node->tty_ops->flush();
    return i;
}

long tty_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    if (!node->tty_ops)
        return -ENOTTY;

    char *str = buffer;
    size_t i = 0;
    struct termios *tio = &file_get_from_node(node)->tio;

    if ((tio->c_lflag & ICANON) == 0) {
        str[i] = node->tty_ops->dequeue(tio->c_cc[VMIN] != 0);

        if (tio->c_lflag & ECHO)
            vfs_write(node, &str[i], 0, 1);
        return 1;
    }

    while (i < len) {
        int c = node->tty_ops->dequeue(true);
        if (c > 0) str[i] = c;
        else continue;
        
        switch (c) {
            case '\033':
                while (node->tty_ops->dequeue(true) != '\0') {}
                break;
            case '\0':
            case '\t':
                break;
            case '\n':
            case '\r':
                if (tio->c_lflag & ECHO)
                    vfs_write(node, "\n", 0, 1);
                str[i++] = '\n';
                str[i] = '\0';
                return i;
            case '\b':
            case 127:
                if (i > 0) {
                    if (tio->c_lflag & ECHO)
                        vfs_write(node, "\b \b", 0, 3);
                    str[i] = '\0';
                    i--;
                }
                break;
            default:
                if (tio->c_lflag & ECHO)
                    vfs_write(node, &c, 0, 1);
                i++;
                break;
        }
    }

    return i;
}

long tty_ioctl(int fd, int op, void *arg) {
    struct file *file = file_get(fd);
    switch (op) {
        case TCGETS:
            return copy_to_user(arg, &file->tio, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:
            return copy_from_user(&file->tio, arg, sizeof(struct termios));
        case TIOCGWINSZ: {
            struct winsize ws;
            framebuffer_get_winsize(&ws);
            return copy_to_user(arg, &ws, sizeof ws);
        }
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP:
            return copy_to_user(arg, &tty_group, sizeof(int));
        case TIOCSPGRP:
            return copy_from_user(&tty_group, arg, sizeof(int));
        // case KDFONTOP: {
        //     struct console_font_op *fop = (struct console_font_op *)arg;
        //     switch (fop->op) {
        //         case KD_FONT_OP_SET: {
        //             unsigned int vpitch = 32;
        //             unsigned int bpc = fop->height;

        //             size_t fontlen = fop->charcount * bpc;
        //             char *fontdata = kmalloc(fontlen);

        //             size_t off = 0;
        //             for (unsigned int i = 0; i < fop->charcount; i++) {
        //                 memcpy(fontdata + off, (void *)fop->data + (i * vpitch), bpc);
        //                 off += bpc;
        //             }
        //             framebuffer_setfont(fontdata, fontlen);
        //             kfree(fontdata);
        //             return 0;
        //         }
        //         default:
        //             return -EINVAL;
        //     }
        // }
        case PIO_UNIMAP:
            return 0;
        case PIO_UNIMAPCLR:
            return 0;
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
}

long console_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    dprintf(LOG_INFO, "");
    write(buffer, len);
    return len;
}

vfs_ops_t console_ops = {
    .write = console_write
};

vfs_tty_ops_t console_tty_ops = {
    .ioctl = tty_ioctl
};

vfs_ops_t tty_ops = {
    .read = tty_read,
    .write = tty_write
};

vfs_tty_ops_t tty_tty_ops = {
    .ioctl = tty_ioctl,
    .enqueue = tty_enqueue,
    .dequeue = tty_dequeue,
    .flush = tty_flush
};

void tty_initialize(void) {
    tty_fifo = fifo_create(1024, int);

    vfs_node_t *console = vfs_create_node("console", VFS_CHARDEVICE);
    console->perms = 0600;
    console->ops = &console_ops;
    console->tty_ops = &console_tty_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), console);

    vfs_node_t *tty = vfs_create_node("tty1", VFS_CHARDEVICE);
    tty->perms = 0600;
    tty->ops = &tty_ops;
    tty->tty_ops = &tty_tty_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), tty);
}