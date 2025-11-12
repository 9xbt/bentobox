#include "kernel/mmu.h"
#include "kernel/termios.h"
#include <stddef.h>
#include <kernel/lfbvideo.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

static void tty_worker_thread(void) {
    vfs_node_t *node = vfs_open(NULL, "/dev/tty1", 0);
    tty_t *tty = node->device;
    int c;
    for (;;) {
        if (fifo_is_empty(tty->ofifo)) {
            this->state = THREAD_PAUSED;
            sched_yield();
        }
        while (fifo_dequeue(tty->ofifo, &c) > 0) {
            if (c > 0)
                putchar(c);
        }
        vfs_wake_waiters(node);
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

void tty_flush(vfs_node_t *node) {
    tty_t *tty = node->device;
    tty->worker->state = THREAD_RUNNING;
}

long tty_poll(vfs_node_t *node, long events) {
    tty_t *tty = node->device;
    if (events & POLLIN) {
        if (!fifo_is_empty(tty->ififo))
            return POLLIN;
    }
    if (events & POLLOUT) {
        if (!fifo_is_full(tty->ofifo))
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
            signal_send_pgrp(tty->pgid, SIGINT);
            puts("^C");
            break;
        case 0x1A:
            signal_send_pgrp(tty->pgid, SIGTSTP);
            puts("^Z");
            break;
        case 0x0C:
            puts("\033[H\033[J");
            break;
        case 0x1C:
            signal_send_pgrp(tty->pgid, SIGQUIT);
            puts("^\\");
            break;
        default:
            return ({ long n = fifo_enqueue(tty->ififo, c); vfs_wake_waiters(node); n; });
    }
    return 0;
}

long tty_dequeue(vfs_node_t *node) {
    tty_t *tty = node->device;
    int c = 0;
    if (fifo_dequeue(tty->ififo, &c) <= 0)
        return -EAGAIN;
    return c;
}

static void tty_handle_sgr(tty_t *tty, const char *buf, size_t len) {
    for (size_t i = 0; i + 8 <= len; i++) {
        if (!memcmp(&buf[i], "\e[?1006", 7) && (buf[i+7] == 'h' || buf[i+7] == 'l'))
            tty->sgr_mode = buf[i + 7] == 'h';
        else if (!memcmp(&buf[i], "\e[?1000", 7) && (buf[i+7] == 'h' || buf[i+7] == 'l'))
            tty->mouse_tracking = buf[i + 7] == 'h';
        else if (!memcmp(&buf[i], "\e[?1002", 7) && (buf[i+7] == 'h' || buf[i+7] == 'l'))
            tty->mouse_tracking = buf[i + 7] == 'h';
    }
}

long tty_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)offset;
    if (!node->tty_ops)
        return -ENOTTY;
    if (!node->tty_ops->enqueue || !node->tty_ops->flush)
        return -EINVAL;

    tty_t *tty = node->device;
    char *buf = (char *)buffer;
    if (len >= 8)
        tty_handle_sgr(tty, buf, len);
    long i;
    for (i = 0; (unsigned)i < len; i++) {
        while (fifo_enqueue(tty->ofifo, buf[i]) < 0) {
            node->tty_ops->flush(node);
            sched_yield();
        }
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
        long c = node->tty_ops->dequeue(node);
        if (c < 0)
            return c;
        str[0] = c;

        if (tio->c_lflag & ECHO)
            vfs_write(node, &str[0], 0, 1);
        return 1;
    }

    size_t i = 0;
    while (i < len) {
        int c = node->tty_ops->dequeue(node);
        if (c > 0) str[i] = c;
        else continue;
        
        switch (c) {
            case '\033':
                for (int j = 0; j < 2; j++)
                    node->tty_ops->dequeue(node);
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
    dprintf(LOG_INFO, "");
    write(buffer, len);
    return len;
}

long console_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    (void)op;
    (void)arg;
    return 0;
}

vfs_ops_t console_ops = {
    .write = console_write
};

vfs_tty_ops_t console_tty_ops = {
    .ioctl = console_ioctl
};

vfs_ops_t tty_ops = {
    .read = tty_read,
    .write = tty_write,
    .poll = tty_poll
};

vfs_tty_ops_t tty_tty_ops = {
    .ioctl = tty_ioctl,
    .enqueue = tty_enqueue,
    .dequeue = tty_dequeue,
    .flush = tty_flush
};

tty_t *tty_create(vfs_node_t *node) {
    tty_t *tty = kmalloc(sizeof(tty_t));
    tty->ififo = fifo_create(512, char);
    tty->ofifo = fifo_create(4096, char);
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
    tty->worker = NULL;
    tty->ioctl = NULL;
    tty->sgr_mode = false;
    tty->mouse_tracking = false;
    node->ops = &tty_ops;
    node->tty_ops = &tty_tty_ops;
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
    tty_t *tty = vfs_open(NULL, "/dev/tty1", 0)->device;

    struct process *proc = sched_new_process("tty", false);
    tty->worker = sched_new_thread(proc, tty_worker_thread, 0, NULL, NULL);
    sched_add_process(proc);
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
    ((tty_t *)tty1->device)->ioctl = tty1_ioctl;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), tty1);
}