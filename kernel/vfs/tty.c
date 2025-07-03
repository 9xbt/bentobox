#include <stdint.h>
#include <stddef.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <kernel/fifo.h>
#include <kernel/ioctl.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/signal.h>

/**
 * TODO: read from FD 0 instead of using getchar() when polling gets implemented
 */

struct fifo tty_fifo;

extern long console_ioctl(int fd_num, int op, void *arg);
extern long ps2_ioctl(int fd_num, int op, void *arg);

long tty_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *str = buffer;
    size_t i = 0;
    struct termios *tio = &this->fd_table[0].tio;

    if ((tio->c_lflag & ICANON) == 0) {
        int c;
    again:
        c = getchar(tio->c_cc[VMIN] != 0);
        if (c > 0) str[i++] = c;
        else goto again;

        switch (c) {
            case 0x3:
                printf("^C\n");
                send_signal(sched_get_foreground(), SIGINT, 0);
                break;
            default:
                if (tio->c_lflag & ECHO)
                    fprintf(stdout, "%c", c);
                break;
        }
        return i;
    }

    while (i < len) {
        int c = getchar(true);
        if (c > 0) str[i] = c;
        else continue;
        
        switch (c) {
            case 0x3:
                printf("^C\n");
                send_signal(sched_get_foreground(), SIGINT, 0);
                break;
            case '\033':
                /* we do not support ANSI escape codes */
                while (getchar(true) != '\0') {}
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

long tty_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    struct fd *fd = fd_get(0);
    if (fd->node == node)
        return 0;
    return vfs_write(fd->node, buffer, offset, len);
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
            strcpy(arg, "/dev/tty");
            return 0;
        case TIOCGWINSZ:
        case TIOCSWINSZ:
        case KDFONTOP:
            return console_ioctl(fd_num, op, arg);
        case PIO_UNIMAP:
        case PIO_UNIMAPCLR:
            return ps2_ioctl(fd_num, op, arg);
        default:
            dprintf("%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
}

//long tty_enqueue(int c) {
//    return !fifo_enqueue(&tty_fifo, c);
//}

void tty_initialize(void) {
    fifo_init(&tty_fifo, 1024);

    vfs_node_t *tty = vfs_create_node("tty", VFS_CHARDEVICE);
    tty->read = tty_read;
    tty->write = tty_write;
    tty->isatty = true;
    tty->tty_ops.ioctl = tty_ioctl;
    vfs_add_device(tty);
}