#include <stdint.h>
#include <stddef.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/fifo.h>
#include <kernel/ioctl.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/video.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/signal.h>

struct fifo tty_fifo;

extern long console_ioctl(int fd_num, int op, void *arg);
extern long ps2_ioctl(int fd_num, int op, void *arg);

void tty_flush(void) {
    int c;
    while (fifo_dequeue(&tty_fifo, &c)) {
        putchar(c);
    }
}

long tty_enqueue(int c) {
    switch (c) {
        case 0x3:
            printf("^C\n");
            send_signal(sched_get_foreground(), SIGINT, 0);
            break;
    }
    return !fifo_enqueue(&tty_fifo, c);
}

long tty_dequeue(bool block) {
    int c = 0;
    while (!fifo_dequeue(&tty_fifo, &c)) {
        if (!block) {
            return -EAGAIN;
        }
        /* since we don't have proper I/O blocking, just halt until an interrupt arrives */
        asm ("hlt");
    }
    return c;
}

long tty_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len && !fifo_is_full(&tty_fifo); i++) {
        fifo_enqueue(&tty_fifo, buf[i]);
    }
    tty_flush();
    return i;
}

long tty_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *str = buffer;
    size_t i = 0;
    struct termios *tio = &fd_get(0)->tio;

    if ((tio->c_lflag & ICANON) == 0) {
        int c;
    again:
        c = tty_dequeue(tio->c_cc[VMIN] != 0);
        if (c > 0) str[i++] = c;
        else goto again;

        if (tio->c_lflag & ECHO)
            fprintf(stdout, "%c", c);
        return i;
    }

    while (i < len) {
        int c = tty_dequeue(true);
        if (c > 0) str[i] = c;
        else continue;
        
        switch (c) {
            case '\033':
                /* we do not support ANSI escape codes */
                while (tty_dequeue(true) != '\0') {}
                break;
            case '\0':
            case '\t':
                break;
            case '\n':
            case '\r':
                if (tio->c_lflag & ECHO)
                    fprintf(stdout, "\n");
                str[i++] = '\n';
                str[i] = '\0';
                return i;
            case '\b':
            case 127:
                if (i > 0) {
                    if (tio->c_lflag & ECHO)
                        fprintf(stdout, "\b \b");
                    str[i] = '\0';
                    i--;
                }
                break;
            default:
                if (tio->c_lflag & ECHO)
                    fprintf(stdout, "%c", c);
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
        case TIOCGNAME:
            strcpy(arg, "/dev/console");
            return 0;
        case TIOCGWINSZ:
            framebuffer_get_winsize((struct winsize *)arg);
            return 0;
        case TIOCSWINSZ:
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

void tty_initialize(void) {
    fifo_init(&tty_fifo, 1024);

    vfs_node_t *tty = vfs_create_node("console", VFS_CHARDEVICE);
    tty->read = tty_read;
    tty->write = tty_write;
    tty->isatty = true;
    tty->tty_ops.ioctl = tty_ioctl;
    vfs_add_device(tty);
}