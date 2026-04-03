#include <stddef.h>
#include <bentobox/setfont.h>
#include <kernel/lfbvideo.h>
#include <kernel/termios.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

static struct thread *tty1_worker = NULL;

static void tty1_worker_thread(void) {
    vfs_node_t *node = vfs_open(NULL, "/dev/tty1", 0).node;
    assert(node);
    tty_t *tty = node->device;
    char c;
    for (;;) {
        if (fifo_is_empty(tty->ofifo)) {
            sched_block(this, 0);
        }
        while (fifo_dequeue(tty->ofifo, &c) > 0) {
            switch (c) {
                case 0x03:
                    puts("^C");
                    continue;
                case 0x1A:
                    puts("^Z");
                    continue;
                case 0x0C:
                    puts("\033[H\033[J");
                    continue;
                case 0x1C:
                    puts("^\\");
                    continue;
                default:
                    putchar(c);
                    continue;
            }
        }
        vfs_wake_waiters(node);
        sched_block(this, 0);
        sched_yield();
    }
}

static long tty1_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    switch (op) {
        case TIOCGWINSZ: {
            struct winsize ws;
            framebuffer_get_winsize(&ws);
            return copy_to_user(arg, &ws, sizeof ws);
        }
        case BBLOADFONT: {
            struct bb_font_op fop;
            if (copy_from_user(&fop, arg, sizeof fop) < 0)
                return -EFAULT;
            void *font = kmalloc(fop.fontlen);
            if (copy_from_user(font, fop.fontdata, fop.fontlen) < 0)
                return -EFAULT;
            framebuffer_setfont((const void *)font, fop.fontlen);
            kfree(font);
            return 0;
        }
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
}

static void tty1_flush(vfs_node_t *node) {
    (void)node;
    sched_wake(tty1_worker);
}

long tty_enqueue(vfs_node_t *node, unsigned char c) {
    tty_t *tty = node->device;
    if (!tty)
        return -EINVAL;
    if (!tty->flush || !c)
        return 0;

    switch (c) {
        case 0x03:
            if (!(tty->tio.c_lflag & ISIG))
                return 0;
            signal_send_pgrp(tty->pgid, SIGINT);
            break;
        case 0x1A:
            if (!(tty->tio.c_lflag & ISIG))
                return 0;
            signal_send_pgrp(tty->pgid, SIGTSTP);
            break;
        case 0x0C:
            if (!(tty->tio.c_lflag & ISIG))
                return 0;
            break;
        case 0x1C:
            if (!(tty->tio.c_lflag & ISIG))
                return 0;
            signal_send_pgrp(tty->pgid, SIGQUIT);
            break;
        case '\r':
            c = '\n';
            /* fallthrough */
        default:
            return ({ long n = fifo_enqueue(tty->ififo, c); tty->flush(node); vfs_wake_waiters(node); n; });
    }

    fifo_enqueue(tty->ofifo, c);
    tty->flush(node);
    return 0;
}

long tty_enqueue_string(vfs_node_t *node, const char *s) {
    long n = 0;
    while (*s)
        n += tty_enqueue(node, *s++);
    return n;
}

void tty_enqueue_sgr_event(vfs_node_t *node, int button, int col, int row, bool release) {
    char buf[32];
    snprintf(buf, sizeof buf, "\e[<%d;%d;%d%c", button, col, row, release ? 'm' : 'M');
    tty_enqueue_string(node, buf);
}

long tty_poll(vfs_node_t *node, long events) {
    tty_t *tty = node->device;
    if (!tty)
        return -EINVAL;

    long revents = 0;
    if (events & POLLIN && !fifo_is_empty(tty->ififo))
        revents |= POLLIN;
    if (events & POLLOUT && !fifo_is_full(tty->ofifo))
        revents |= POLLOUT;
    return revents;
}

static void tty_handle_sgr(tty_t *tty, const char *buf, size_t len) {
    for (size_t i = 0; i + 8 <= len; i++) {
        if (!memcmp(&buf[i], "\e[?1006", 7) && (buf[i+7] == 'h' || buf[i+7] == 'l'))
            tty->sgr_mode = buf[i+7] == 'h';
        else if (!memcmp(&buf[i], "\e[?1000", 7) && (buf[i+7] == 'h' || buf[i+7] == 'l'))
            tty->mouse_tracking = buf[i+7] == 'h';
        else if (!memcmp(&buf[i], "\e[?1002", 7) && (buf[i+7] == 'h' || buf[i+7] == 'l'))
            tty->mouse_tracking = buf[i+7] == 'h';
    }
}

long tty_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    tty_t *tty = node->device;
    if (!tty)
        return -EINVAL;
    if (fifo_is_full(tty->ofifo)) {
        vfs_wake_waiters(node);
        return -EAGAIN;
    }

    char *buf = (char *)buffer;
    if (len >= 8)
        tty_handle_sgr(tty, buf, len);

    long i;
    for (i = 0; (unsigned)i < len; i++) {
        if (tty->tio.c_oflag & OPOST) {
            char c = buf[i];
            if ((tty->tio.c_oflag & ONLCR) && c == '\n') {
                if (fifo_enqueue(tty->ofifo, '\r') < 0)
                    break;
            }
        }
        
        if (fifo_enqueue(tty->ofifo, buf[i]) < 0)
            break;
    }

    tty->flush(node);
    vfs_wake_waiters(node);
    return i;
}

long tty_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    tty_t *tty = node->device;
    if (!tty)
        return -EINVAL;
    if (fifo_is_empty(tty->ififo)) {
        vfs_wake_waiters(node);
        return -EAGAIN;
    }

    char *buf = (char *)buffer;

    if (!(tty->tio.c_lflag & ICANON)) {
        long i;
        for (i = 0; (unsigned)i < len; i++) {
            if (fifo_dequeue(tty->ififo, &buf[i]) < 0)
                break;
            if (tty->tio.c_lflag & ECHO && fifo_enqueue(tty->ofifo, buf[i]) < 0)
                break;
        }
        
        vfs_wake_waiters(node);
        return i;
    }
    
    size_t i = 0;
    while (i < len) {
        char c;
        if (fifo_dequeue(tty->ififo, &c) < 0) {
            vfs_poll(node, POLLIN, -1);
            continue;
        }

        if (c == '\r')
            c = '\n';
        else if (c == 127)
            c = '\b';
        if (c == '\b' && !i)
            c = '\0';

        if (tty->tio.c_lflag & ECHO) {
            if (c == '\b') {
                fifo_enqueue(tty->ofifo, '\b');
                fifo_enqueue(tty->ofifo, ' ');
                fifo_enqueue(tty->ofifo, '\b');
            } else {
                fifo_enqueue(tty->ofifo, c);
            }
            tty->flush(node);
        }

        switch (c) {
            case '\0':
                break;
            case '\n':
                buf[i++] = c;
                buf[i] = '\0';

                vfs_wake_waiters(node);
                return i;
            case '\b':
                if (!i)
                    break;
                buf[i--] = '\0';
                break;
            default:
                buf[i++] = c;
                break;
        }
    }

    vfs_wake_waiters(node);
    return i;
}

long tty_ioctl(vfs_node_t *node, int op, void *arg) {
    tty_t *tty = node->device;
    switch (op) {
        case TCGETS:
            return copy_to_user(arg, &tty->tio, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:
            return copy_from_user(&tty->tio, arg, sizeof(struct termios));
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP:
            return copy_to_user(arg, &tty->pgid, sizeof tty->pgid);
        case TIOCSPGRP:
            return copy_from_user(&tty->pgid, arg, sizeof tty->pgid);
        case TCXONC:
            // TODO
            return 0;
        default:
            if (!tty->ioctl)
                return -EINVAL;
            return tty->ioctl(node, op, arg);
    }
}

long console_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    char *s = kmalloc(len + 1);
    memcpy(s, buffer, len);
    s[len] = 0;
    dprintf(LOG_INFO, "%s", s);
    return len;
}

long console_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    (void)op;
    (void)arg;
    return 0;
}

vfs_ops_t console_ops = {
    .write = console_write,
    .ioctl = console_ioctl
};

vfs_ops_t tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .poll = tty_poll,
    .ioctl = tty_ioctl
};

tty_t *tty_create(vfs_node_t *node) {
    tty_t *tty = kmalloc(sizeof(tty_t));
    tty->ioctl = NULL;
    tty->flush = NULL;
    tty->locked = 0;
    tty->pgid = 0;
    tty->sgr_mode = false;
    tty->mouse_tracking = false;
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
    memset(&tty->ws, 0, sizeof tty->ws);
    tty->ififo = fifo_create(4096, char);
    tty->ofifo = fifo_create(4096, char);

    node->ops = &tty_ops;
    node->inode = 100000;
    return tty;
}

void tty_destroy(vfs_node_t *node) {
    tty_t *tty = node->device;
    fifo_destroy(tty->ififo);
    fifo_destroy(tty->ofifo);
    kfree(node->device);
    node->device = NULL;
    return;
}

void tty_spawn_worker(void) {
    if (framebuffer) {
        struct process *proc = sched_new_process("tty worker", false);
        tty1_worker = sched_new_thread(proc, tty1_worker_thread, 0, NULL, NULL, NULL, 0, NULL);
        sched_add_process(proc);
    }
}

void tty_initialize(void) {
    vfs_node_t *console = devfs_create_node("console", VFS_CHARDEVICE);
    console->perms = 0600;
    console->ops = &console_ops;

    if (framebuffer) {
        vfs_node_t *tty1 = devfs_create_numbered(DEVFS_TTY);
        tty1->perms = 0600;
        tty1->device = tty_create(tty1);
        ((tty_t *)tty1->device)->ioctl = tty1_ioctl;
        ((tty_t *)tty1->device)->flush = tty1_flush;
    }
}