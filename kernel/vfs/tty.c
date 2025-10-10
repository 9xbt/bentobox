#include <stddef.h>
#include <kernel/lfbvideo.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

static void tty_flush(vfs_node_t *node) {
    tty_t *tty = node->device;
    int c;
    while (fifo_dequeue(tty->fifo, &c) > 0) {
        if (c > 0)
            putchar(c);
    }
}

long tty_poll(vfs_node_t *node, long events) {
    tty_t *tty = node->device;
    if (events & POLLIN) {
        if (!fifo_is_empty(tty->fifo))
            return POLLIN;
    }
    if (events & POLLOUT) {
        return POLLOUT;
    }
    return 0;
}

long tty_enqueue(vfs_node_t *node, unsigned char c) {
    if (!c)
        return 0;
    tty_t *tty = node->device;
    switch (c) {
        case 0x03:
            signal_send(sched_find_in_group(tty->pgid), SIGINT);
            puts("^C\n");
            break;
        case 0x1A:
            signal_send(sched_find_in_group(tty->pgid), SIGTSTP);
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
            return ({ long n = fifo_enqueue(tty->fifo, c); vfs_wake_waiters(node); n; });
    }
    return 0;
}

long tty_dequeue(vfs_node_t *node, bool block) {
    tty_t *tty = node->device;
    while (fifo_is_empty(tty->fifo)) {
        if (!block)
            return -EAGAIN;
    }
    int c = 0;
    if (fifo_dequeue(tty->fifo, &c) <= 0)
        return -EAGAIN;
    return c;
}

long tty_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    if (!node->tty_ops)
        return -ENOTTY;
    if (!node->tty_ops->enqueue || !node->tty_ops->flush)
        return -EINVAL;

    char *buf = (char *)buffer;
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (node->tty_ops->enqueue(node, buf[i]) < 0)
            break;
    }
    node->tty_ops->flush(node);
    return i;
}

long tty_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    if (!node->tty_ops)
        return -ENOTTY;

    char *str = buffer;
    tty_t *tty = node->device;
    struct termios *tio = &tty->tio;

    if ((tio->c_lflag & ICANON) == 0) {
        long c;
        while ((c = node->tty_ops->dequeue(node, tio->c_cc[VMIN] != 0)) < 0) {}
        str[0] = c;

        if (tio->c_lflag & ECHO)
            vfs_write(node, &str[0], 0, 1);
        return 1;
    }

    size_t i = 0;
    while (i < len) {
        int c = node->tty_ops->dequeue(node, true);
        if (c > 0) str[i] = c;
        else continue;
        
        switch (c) {
            case '\033':
                while (node->tty_ops->dequeue(node, true) != '\0') {}
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

static long tty_ioctl(vfs_node_t *node, int op, void *arg) {
    tty_t *tty = node->device;
    switch (op) {
        case TCGETS:
            return copy_to_user(arg, &tty->tio, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:
            return copy_from_user(&tty->tio, arg, sizeof(struct termios));
        case TIOCGWINSZ: {
            struct winsize ws;
            framebuffer_get_winsize(&ws);
            return copy_to_user(arg, &ws, sizeof ws);
        }
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP:
            return copy_to_user(arg, &tty->pgid, sizeof tty->pgid);
        case TIOCSPGRP:
            return copy_from_user(&tty->pgid, arg, sizeof tty->pgid);
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
    .write = tty_write,
    .poll = tty_poll
};

tty_t *tty_create(vfs_node_t *node) {
    tty_t *tty = kmalloc(sizeof(tty_t));
    tty->fifo = fifo_create(1024, char);
    tty->node = node;
    tty->pgid = 0;
    tty->tio.c_iflag = BRKINT | ICRNL | IXON;
    tty->tio.c_oflag = OPOST | ONLCR;
    tty->tio.c_cflag = CS8 | CREAD;
    tty->tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    tty->tio.c_cc[VINTR] = 3;
    tty->tio.c_cc[VQUIT] = 28;
    tty->tio.c_cc[VERASE] = 127;
    tty->tio.c_cc[VKILL] = 21;
    tty->tio.c_cc[VEOF] = 4;
    tty->tio.c_cc[VTIME] = 0;
    tty->tio.c_cc[VMIN] = 1;
    tty->tio.c_cc[VSTART] = 17;
    tty->tio.c_cc[VSTOP] = 19;
    tty->tio.c_cc[VSUSP] = 26;
    node->ops = &tty_ops;
    node->tty_ops = kmalloc(sizeof(vfs_tty_ops_t));
    node->tty_ops->ioctl = NULL;
    node->tty_ops->enqueue = tty_enqueue;
    node->tty_ops->dequeue = tty_dequeue;
    node->tty_ops->flush = NULL;
    return tty;
}

void tty_destroy(vfs_node_t *node) {
    tty_t *tty = node->device;
    fifo_destroy(tty->fifo);
    kfree(node->tty_ops);
    node->tty_ops = NULL;
    kfree(node->device);
    node->device = NULL;
    return;
}

void tty_initialize(void) {
    vfs_node_t *console = vfs_create_node("console", VFS_CHARDEVICE);
    console->perms = 0600;
    console->ops = &console_ops;
    console->tty_ops = &console_tty_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), console);

    vfs_node_t *tty1 = vfs_create_node("tty1", VFS_CHARDEVICE);
    tty1->perms = 0600;
    tty1->device = tty_create(tty1);
    tty1->tty_ops->ioctl = tty_ioctl;
    tty1->tty_ops->flush = tty_flush;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), tty1);
}