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

static bool serial_works = false;
static spinlock_t serial_lock = 0;
static vfs_node_t *ttyS0;

void serial_install(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0x07);
    outb(COM1 + 4, 0x0B);
    
    outb(COM1 + 7, 0xAB);
    if (inb(COM1 + 7) == 0xAB)
        serial_works = true;
}

int serial_is_bus_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

int serial_is_data_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

void serial_putchar(char c) {
    while (serial_is_bus_empty() == 0)
        __builtin_ia32_pause();
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

static void serial_tty_worker_thread(void) {
    vfs_node_t *node = vfs_open(NULL, "/dev/ttyS0", 0);
    tty_t *tty = node->device;
    char c;
    for (;;) {
        if (fifo_is_empty(tty->ofifo)) {
            this->state = THREAD_PAUSED;
            sched_yield();
        }

        acquire(&serial_lock);
        while (fifo_dequeue(tty->ofifo, &c) > 0) {
            serial_putchar(c);
        }
        release(&serial_lock);

        vfs_wake_waiters(node);
    }
}

static long serial_tty_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    switch (op) {
        case TIOCGWINSZ: {
            struct winsize ws = {
                .ws_row = 25,
                .ws_col = 80
            };
            return copy_to_user(arg, &ws, sizeof ws);
        }
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
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
    if (!serial_works)
        return;

    ttyS0 = vfs_create_node("ttyS0", VFS_CHARDEVICE);
    ttyS0->perms = 0600;
    ttyS0->device = tty_create(ttyS0);
    ((tty_t *)ttyS0->device)->ioctl = serial_tty_ioctl;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), ttyS0);
    
    irq_register(4, irq4_handler);
    ioapic_redirect_irq(0, 36, 4, false);
    outb(COM1 + 1, 0x01);

    tty_t *tty = ttyS0->device;

    struct process *proc = sched_new_process("serial tty", false);
    tty->worker = sched_new_thread(proc, serial_tty_worker_thread, 0, NULL, NULL, NULL);
    sched_add_process(proc);
}