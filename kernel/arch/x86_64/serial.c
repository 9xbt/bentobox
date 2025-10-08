#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/spinlock.h>
#include <kernel/termios.h>
#include <kernel/context.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/fifo.h>
#include <kernel/file.h>
#include <kernel/tty.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

static uint16_t serial_base = COM1;
static spinlock_t serial_lock = 0;
static struct fifo *serial_fifo;
static int serial_tty_group = 1;

void serial_install(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 0, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    outb(COM1 + 4, 0x1E);
    outb(COM1 + 0, 0x55);

    if (inb(COM1) != 0x55) {
        dprintf(LOG_WARNING, "\033[93muart:\033[0m falling back to port 0x%x\n", DEBUGCON);
        serial_base = DEBUGCON;
        return;
    }

    outb(COM1 + 4, 0x0F);
}

int serial_is_bus_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

int serial_is_data_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

void serial_putchar(char c) {
    while (serial_is_bus_empty() == 0) {}
    if (c == '\n')
        outb(COM1, '\r');
    outb(COM1, c);
}

void serial_write(const char *s, size_t len) {
    acquire(&serial_lock);
    for (size_t i = 0; i < len; i++) {
        serial_putchar(s[i]);
    }
    release(&serial_lock);
}

void serial_puts(const char *str) {
    serial_write(str, strlen(str));
}

long serial_tty_ioctl(int fd, int op, void *arg) {
    struct file *file = file_get(fd);
    switch (op) {
        case TCGETS:
            return copy_to_user(arg, &file->node->tio, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:
            return copy_from_user(&file->node->tio, arg, sizeof(struct termios));
        case TIOCGWINSZ: {
            struct winsize ws = {
                .ws_row = 25,
                .ws_col = 80
            };
            return copy_to_user(arg, &ws, sizeof ws);
        }
        case TIOCSWINSZ:
            return 0;
        case TIOCGPGRP:
            return copy_to_user(arg, &serial_tty_group, sizeof(int));
        case TIOCSPGRP:
            return copy_from_user(&serial_tty_group, arg, sizeof(int));
        default:
            dprintf(LOG_DEBUG, "\033[93m%s\033[0m: function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
}

void serial_tty_flush(void) {
    int c;
    while (fifo_dequeue(serial_fifo, &c) > 0) {
        if (c > 0) serial_putchar(c);
    }
}

long serial_tty_enqueue(int c) {
    switch (c) {
        case 12:
            serial_puts("\033[H\033[J");
            return 0;
    }
    return fifo_enqueue(serial_fifo, c);
}

void serial_tty_enqueue_string(char *str) {
    while (*str) {
        serial_tty_enqueue(*str++);
    }
}

long serial_tty_dequeue(bool block) {
    int c = 0;
    while (fifo_dequeue(serial_fifo, &c) > 0) {
        if (!block) {
            return -EAGAIN;
        }
    }
    return c;
}

void irq4_handler(struct registers *r) {
    (void)r;
    uint8_t iir = inb(COM1 + 2);
    if ((iir & 0x06) == 0x04) {
        serial_tty_enqueue(inb(COM1));
    }
    lapic_eoi();
}

vfs_tty_ops_t serial_tty_ops = {
    .ioctl = serial_tty_ioctl,
    .enqueue = serial_tty_enqueue,
    .dequeue = serial_tty_dequeue,
    .flush = serial_tty_flush
};

void serial_initialize(void) {
    serial_fifo = fifo_create(64, int);
    irq_register(4, irq4_handler);
    ioapic_redirect_irq(0, 36, 4, false);
    outb(COM1 + 1, 0x01);

    vfs_node_t *ttyS0 = vfs_create_node("ttyS0", VFS_CHARDEVICE);
    ttyS0->perms = 0600;
    ttyS0->ops = &tty_ops;
    ttyS0->tty_ops = &serial_tty_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), ttyS0);
}