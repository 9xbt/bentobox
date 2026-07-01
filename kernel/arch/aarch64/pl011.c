#include <stdint.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/ringbuffer.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/assert.h>
#include <kernel/errno.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>

#define UARTDR    0x000
#define UARTFR    0x018
#define UARTLCR_H 0x02C
#define UARTCR    0x030
#define UARTIMSC  0x038
#define UARTICR   0x044

#define UARTFR_TXFF (1 << 5)
#define UARTFR_BUSY (1 << 3)

static volatile uint32_t *pl011_base = NULL;
static spinlock_t uart_lock = 0;
static vfs_node_t *ttyS0;

uint32_t pl011_read(uint32_t offset) {
    return pl011_base ? pl011_base[offset / sizeof(uint32_t)] : 0;
}

void pl011_write(uint32_t offset, uint32_t value) {
    if (pl011_base)
        pl011_base[offset / sizeof(uint32_t)] = value;
}

int pl011_is_bus_empty(void) {
    return !(pl011_read(UARTFR) & UARTFR_TXFF);
}

void uart_putchar(char c) {
    while (!pl011_is_bus_empty());
    
    if (c == '\n') {
        pl011_write(UARTDR, '\r');
        while (!pl011_is_bus_empty());
    }
    
    pl011_write(UARTDR, c);
}

void uart_write(int loglevel, const char *s, size_t len) {
    (void)loglevel;
    acquire(&uart_lock);
    for (size_t i = 0; i < len; i++) {
        uart_putchar(s[i]);
    }
    release(&uart_lock);
}

void uart_puts(const char *str) {
    uart_write(0, str, strlen(str));
}

static struct thread *uart_tty_worker = NULL;

static void uart_tty_worker_thread(void) {
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
                    uart_puts("^C");
                    continue;
                case 0x1A:
                    uart_puts("^Z");
                    continue;
                case 0x0C:
                    uart_puts("\033[H\033[J");
                    continue;
                case 0x1C:
                    uart_puts("^\\");
                    continue;
                default:
                    uart_putchar(c);
                    continue;
            }
        }

        vfs_wake_waiters(node);
        sched_block(this, 0);
    }
}

static void uart_tty_flush(vfs_node_t *node) {
    (void)node;
    if (uart_tty_worker)
        sched_wake(uart_tty_worker);
}

static long uart_tty_ioctl(vfs_node_t *node, int op, void *arg) {
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

void pl011_irq_handler() {
    tty_enqueue(ttyS0, pl011_read(UARTDR) & 0xFF);
    pl011_write(UARTICR, 0x10);
}

void pl011_install(void) {
    pl011_base = VIRTUAL_HHDM(0x09000000);
    mmu_map(kernel_pd, (void *)pl011_base, (void *)0x09000000, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN);
    
    uart_puts((char *)kernel_rb->buffer);
}

void pl011_initialize(void) {
    pl011_write(UARTCR, 0);
    pl011_write(UARTLCR_H, 0x70);
    pl011_write(UARTCR, 0x301);
    pl011_write(UARTIMSC, 0x10);

    ttyS0 = devfs_create_numbered(DEVFS_STTY);
    ttyS0->perms = 0600;
    ttyS0->device = tty_create(ttyS0);
    ((tty_t *)ttyS0->device)->ioctl = uart_tty_ioctl;
    ((tty_t *)ttyS0->device)->flush = uart_tty_flush;

    struct process *proc = sched_new_process("uart tty", false);
    uart_tty_worker = sched_new_thread(proc, uart_tty_worker_thread, 0, NULL, NULL, NULL, 0, NULL);
    sched_add_process(proc);

    irq_allocate(gic_domain, pl011_irq_handler, 33, 33);
}