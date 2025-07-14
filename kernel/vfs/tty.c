#include <stdint.h>
#include <stddef.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/fifo.h>
#include <ioctls.h>
#include <errno.h>
#include <kernel/sched.h>
#include <kernel/video.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/signal.h>

static struct fifo *tty_fifo;
static vfs_node_t *console, *tty;

extern long console_ioctl(int fd_num, int op, void *arg);
extern long ps2_ioctl(int fd_num, int op, void *arg);
extern long serial_ioctl(int fd_num, int op, void *arg);

void tty_flush(void) {
    int c;
    while (fifo_dequeue(tty_fifo, &c)) {
        if (c > 0) putchar(c);
    }
}

long tty_poll(struct vfs_node *node) {
    if (!fifo_is_empty(tty_fifo))
        return -1UL;
    return 0;
}

long tty_enqueue(int c) {
    switch (c) {
        case 0x3:
            printf("^C\n");
            signal_send(sched_get_foreground(), SIGINT, 0);
            return 0;
    }
    vfs_wake_up_sleeping(console);
    vfs_wake_up_sleeping(tty);
    return !fifo_enqueue(tty_fifo, c);
}

long tty_dequeue(bool block) {
    int c = 0;
    while (!fifo_dequeue(tty_fifo, &c)) {
        if (!block) {
            return -EAGAIN;
        }
        vfs_poll(console);
    }
    return c;
}

long tty_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (node->tty_ops.enqueue(buf[i]))
            break;
    }
    node->tty_ops.flush();
    return i;
}

long tty_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *str = buffer;
    size_t i = 0;
    struct termios *tio = &fd_get(0)->tio;

    if ((tio->c_lflag & ICANON) == 0) {
        int c;
    again:
        c = node->tty_ops.dequeue(tio->c_cc[VMIN] != 0);
        if (c > 0) str[i++] = c;
        else goto again;

        if (tio->c_lflag & ECHO)
            fprintf(stdout, "%c", c);
        return i;
    }

    while (i < len) {
        int c = node->tty_ops.dequeue(true);
        if (c > 0) str[i] = c;
        else continue;
        
        switch (c) {
            case '\033':
                /* we do not support ANSI escape codes */
                while (node->tty_ops.dequeue(true) != '\0') {}
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

long tty_ioctl(int fd_num, int op, void *arg) {
    struct fd *fd = fd_get(fd_num);
    switch (op) {
        case TCGETS:
            memcpy(arg, &fd->tio, sizeof(struct termios));
            return 0;
        case TCSETS:
        case TCSETSW:
            memcpy(&fd->tio, arg, sizeof(struct termios));
            return 0;
        case TIOCGWINSZ:
            framebuffer_get_winsize((struct winsize *)arg);
            return 0;
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP:
            *(int *)arg = this->pid;
            return 0;
        case TIOCSPGRP:
            return 0;
        case KDFONTOP: {
            struct console_font_op *fop = (struct console_font_op *)arg;
            switch (fop->op) {
                case KD_FONT_OP_SET: {
                    unsigned int vpitch = 32;
                    unsigned int bpc = fop->height;

                    size_t fontlen = fop->charcount * bpc;
                    char *fontdata = kmalloc(fontlen);

                    size_t off = 0;
                    for (unsigned int i = 0; i < fop->charcount; i++) {
                        memcpy(fontdata + off, (void *)fop->data + (i * vpitch), bpc);
                        off += bpc;
                    }
                    framebuffer_setfont(fontdata, fontlen);
                    kfree(fontdata);
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

extern long serial_tty_poll(struct vfs_node *node);

void tty_initialize(void) {
    tty_fifo = fifo_create(1024);

    console = vfs_create_node("console", VFS_CHARDEVICE);
    console->perms = 0600;
    console->read = tty_read;
    console->write = tty_write;
    console->isatty = true;
    console->poll = tty_poll;
    console->tty_ops.ioctl = tty_ioctl;
    console->tty_ops.flush = tty_flush;
    console->tty_ops.enqueue = tty_enqueue;
    console->tty_ops.dequeue = tty_dequeue;
    vfs_add_device(console);

    tty = vfs_create_node("tty", VFS_CHARDEVICE);
    tty->perms = 0666;
    tty->read = tty_read;
    tty->write = tty_write;
    tty->isatty = true;
    tty->poll = tty_poll;
    tty->tty_ops.ioctl = tty_ioctl;
    tty->tty_ops.flush = tty_flush;
    tty->tty_ops.enqueue = tty_enqueue;
    tty->tty_ops.dequeue = tty_dequeue;
    vfs_add_device(tty);

    vfs_node_t *serial_tty = vfs_create_node("ttyS0", VFS_CHARDEVICE);
    serial_tty->perms = 0666;
    serial_tty->read = tty_read;
    serial_tty->write = tty_write;
    serial_tty->isatty = true;
    serial_tty->poll = serial_tty_poll;
    serial_tty->tty_ops.ioctl = serial_ioctl;
    serial_tty->tty_ops.flush = serial_tty_flush;
    serial_tty->tty_ops.enqueue = serial_tty_enqueue;
    serial_tty->tty_ops.dequeue = serial_tty_dequeue;
    vfs_add_device(serial_tty);
}