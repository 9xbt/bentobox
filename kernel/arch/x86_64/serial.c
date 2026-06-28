#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/spinlock.h>
#include <kernel/termios.h>
#include <kernel/context.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/fifo.h>
#include <kernel/file.h>
#include <kernel/tty.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

static bool com1_works = false;
static spinlock_t serial_lock = 0;
static vfs_node_t *ttyS0;

bool serial_initialize(uint16_t port, uint8_t divisor) {
    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port + 0, divisor);
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);
    outb(port + 2, 0x07);
    outb(port + 4, 0x0B);
    
    outb(port + 7, 0xAB);
    bool works = inb(port + 7) == 0xAB;
    if (port == COM1 && works)
        com1_works = true;
    return works;
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
    outb(COM1, c);
}

void serial_write(int level, const char *s, size_t len) {
    (void)level;
    acquire(&serial_lock);
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n')
            serial_putchar('\r');
        serial_putchar(s[i]);
    }
    release(&serial_lock);
}

void serial_puts(const char *str) {
    serial_write(0, str, strlen(str));
}

static struct thread *serial_tty_worker = NULL;

static void serial_tty_worker_thread(void) {
    vfs_node_t *node = vfs_open(NULL, "/dev/ttyS0", 0).node;
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
                    serial_puts("^C");
                    continue;
                case 0x1A:
                    serial_puts("^Z");
                    continue;
                case 0x0C:
                    serial_puts("\033[H\033[J");
                    continue;
                case 0x1C:
                    serial_puts("^\\");
                    continue;
                default:
                    serial_putchar(c);
                    continue;
            }
        }

        vfs_wake_waiters(node);
        sched_block(this, 0);
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
        case TIOCSWINSZ:
            return 0;
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m function 0x%lx not implemented\n", __func__, op);
            return -EINVAL;
    }
}

static void serial_tty_flush(vfs_node_t *node) {
    (void)node;
    if (serial_tty_worker)
        sched_wake(serial_tty_worker);
}

void serial_irq_handler() {
    uint8_t iir = inb(COM1 + 2);
    if ((iir & 0x06) == 0x04) {
        tty_enqueue(ttyS0, inb(COM1));
    }
}

void serial_install(void) {
    if (!com1_works)
        return;

    ttyS0 = devfs_create_numbered(DEVFS_STTY);
    ttyS0->perms = 0600;
    ttyS0->device = tty_create(ttyS0);
    ((tty_t *)ttyS0->device)->ioctl = serial_tty_ioctl;
    ((tty_t *)ttyS0->device)->flush = serial_tty_flush;
    
    irq_allocate(ioapic_domain, serial_irq_handler, 4, -1);
    // ioapic_redirect_irq(0, 36, 4, false);
    outb(COM1 + 1, 0x01);

    struct process *proc = sched_new_process("ttyS0 worker", false);
    serial_tty_worker = sched_new_thread(proc, serial_tty_worker_thread, 0, NULL, NULL, NULL, 0, NULL);
    sched_add_process(proc);
}