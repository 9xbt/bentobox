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
static vfs_node_t *ttyS0;

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

static void serial_tty_flush(vfs_node_t *node) {
    acquire(&serial_lock);
    tty_t *tty = node->device;
    int c;
    while (fifo_dequeue(tty->fifo, &c) > 0) {
        if (c > 0)
            serial_putchar(c);
    }
    release(&serial_lock);
}

static long serial_tty_ioctl(vfs_node_t *node, int op, void *arg) {
    tty_t *tty = node->device;
    switch (op) {
        case TCGETS:
            return copy_to_user(arg, &tty->tio, sizeof(struct termios));
        case TCSETS:
        case TCSETSW:
            return copy_from_user(&tty->tio, arg, sizeof(struct termios));
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
            return copy_to_user(arg, &tty->pgid, sizeof(int));
        case TIOCSPGRP:
            return copy_from_user(&tty->pgid, arg, sizeof(int));
        default:
            dprintf(LOG_DEBUG, "\033[93m%s\033[0m: function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
}

void irq4_handler(struct registers *r) {
    (void)r;
    uint8_t iir = inb(COM1 + 2);
    if ((iir & 0x06) == 0x04) {
        ttyS0->tty_ops->enqueue(ttyS0, inb(COM1));
    }
    lapic_eoi();
}

void serial_initialize(void) {
    ttyS0 = vfs_create_node("ttyS0", VFS_CHARDEVICE);
    ttyS0->perms = 0600;
    ttyS0->device = tty_create(ttyS0);
    ttyS0->tty_ops->ioctl = serial_tty_ioctl;
    ttyS0->tty_ops->flush = serial_tty_flush;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), ttyS0);
    
    irq_register(4, irq4_handler);
    ioapic_redirect_irq(0, 36, 4, false);
    outb(COM1 + 1, 0x01);
}