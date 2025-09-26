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
#include <kernel/vfs.h>

static uint16_t serial_base = COM1;
static spinlock_t serial_lock = 0;
static struct fifo *serial_fifo;

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

void serial_puts(const char *str) {
    acquire(&serial_lock);
    while (*str) {
        serial_putchar(*str++);
    }
    release(&serial_lock);
}

long serial_tty_ioctl(int fd, int op, void *arg) {
    struct file *file = file_get(fd);
    switch (op) {
        case TCGETS:
            memcpy(arg, &file->tio, sizeof(struct termios));
            return 0;
        case TCSETS:
        case TCSETSW:
            memcpy(&file->tio, arg, sizeof(struct termios));
            return 0;
        case TIOCGWINSZ: {
            struct winsize *ws = (struct winsize *)arg;
            ws->ws_row = 25;
            ws->ws_col = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case TIOCSWINSZ:
            return 0;
        // case TIOCGPGRP:
        //     return 0;
        // case TIOCSPGRP:
        //     return 0;
        default:
            dprintf(LOG_INFO, "%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
}

void serial_tty_flush(void) {
    int c;
    while (fifo_dequeue(serial_fifo, &c)) {
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
    do {
        serial_tty_enqueue(*str);
    } while (*str++);
}

long serial_tty_dequeue(bool block) {
    int c = 0;
    while (!fifo_dequeue(serial_fifo, &c)) {
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

    vfs_node_t *console = vfs_create_node("ttyS0", VFS_CHARDEVICE);
    console->perms = 0600;
    console->ops = &tty_ops;
    console->tty_ops = &serial_tty_ops;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), console);
}