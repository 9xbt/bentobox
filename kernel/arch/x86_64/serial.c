#include <errno.h>
#include <stddef.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/vfs.h>
#include <kernel/fifo.h>
#include <ioctls.h>
#include <kernel/sched.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/spinlock.h>

#define COM1 0x3f8

uint16_t serial_base = COM1;
atomic_flag serial_lock = ATOMIC_FLAG_INIT;
struct fifo *serial_fifo;
bool kmsg_silence = false;

char serial_ringbuffer[1024];

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
        serial_base = 0;
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

char serial_read_char(void) {
    int c = 0;
    while (!fifo_dequeue(serial_fifo, &c)) {
        sched_yield();
    }
    return c;
}

void serial_write_char(char c) {
    while (serial_is_bus_empty() == 0);
    if (c == '\n')
        outb(COM1, '\r');
    outb(COM1, c);
}

void serial_puts(char *str) {
    static size_t offset = 0;

    acquire(&serial_lock);
    while (*str) {
        serial_ringbuffer[offset] = *str;
        offset = (offset + 1) % sizeof(serial_ringbuffer);
        serial_write_char(*str++);
    }
    release(&serial_lock);
}

int dprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};
    int ret = vsprintf(buf, fmt, args);
    if (serial_base == COM1) {
        serial_puts(buf);
    }
    if (!kmsg_silence) {
        puts(buf);
    }
    va_end(args);
    return ret;
}

long serial_ioctl(int fd_num, int op, void *arg) {
    struct fd *fd = &this->fd_table[fd_num];
    switch (op) {
        case TCGETS:
            memcpy(arg, &fd->tio, sizeof(struct termios));
            return 0;
        case TCSETS:
        case TCSETSW:
            memcpy(&fd->tio, arg, sizeof(struct termios));
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
        case TIOCGPGRP:
            *(int *)arg = this->pid;
            return 0;
        case TIOCSPGRP:
            return 0;
        default:
            dprintf("%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
}

void serial_tty_flush(void) {
    int c;
    while (fifo_dequeue(serial_fifo, &c)) {
        if (c > 0) serial_write_char(c);
    }
}

long serial_tty_poll(struct vfs_node *node) {
    if (!fifo_is_empty(serial_fifo))
        return -1UL;
    return 0;
}

long serial_tty_enqueue(int c) {
    switch (c) {
        case 12:
            serial_puts("\033[H\033[J");
            return 0;
    }
    return !fifo_enqueue(serial_fifo, c);
}

long serial_tty_dequeue(bool block) {
    int c = 0;
    while (!fifo_dequeue(serial_fifo, &c)) {
        if (!block) {
            return -EAGAIN;
        }
        vfs_poll(vfs_open(NULL, "/dev/ttyS0", false, false));
    }
    return c;
}

void irq4_handler(struct registers *r) {
    uint8_t iir = inb(COM1 + 2);
    if ((iir & 0x06) == 0x04) {
        serial_tty_enqueue(inb(COM1));
    }
    lapic_eoi();
}

void serial_initialize(void) {
    serial_fifo = fifo_create(64);
    irq_register(4, irq4_handler);
    outb(COM1 + 1, 0x01);
}