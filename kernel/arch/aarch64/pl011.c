#include <stdint.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/printf.h>
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

void uart_write(const char *s, size_t len) {
    acquire(&uart_lock);
    for (size_t i = 0; i < len; i++) {
        uart_putchar(s[i]);
    }
    release(&uart_lock);
}

void uart_puts(const char *str) {
    uart_write(str, strlen(str));
}

static void serial_tty_worker_thread(void) {
    tty_t *tty = vfs_open(NULL, "/dev/ttyS0", 0)->device;
    int c;
    for (;;) {
        acquire(&uart_lock);
        while (fifo_dequeue(tty->ofifo, &c) > 0) {
            if (c > 0)
                uart_putchar(c);
        }
        release(&uart_lock);

        this->state = THREAD_PAUSED;
        sched_yield();
    }
}

static long pl011_tty_ioctl(vfs_node_t *node, int op, void *arg) {
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

void pl011_irq_handler(struct registers *r) {
    (void)r;
    ttyS0->tty_ops->enqueue(ttyS0, pl011_read(UARTDR) & 0xFF);
    pl011_write(UARTICR, 0x10);
}

void pl011_install(void) {
    pl011_base = VIRTUAL_HHDM(0x09000000);
    mmu_map(kernel_pd, (void *)pl011_base, (void *)0x09000000, PTE_VALID | PTE_AF | PTE_RW | PTE_PXN);
    dprintf(LOG_INFO, "\033[93muart:\033[0m mapped pl011 base\n");
}

void pl011_initialize(void) {
    pl011_write(UARTCR, 0);
    pl011_write(UARTLCR_H, 0x70);
    pl011_write(UARTCR, 0x301);
    pl011_write(UARTIMSC, 0x10); 

    ttyS0 = vfs_create_node("ttyS0", VFS_CHARDEVICE);
    ttyS0->perms = 0600;
    ttyS0->device = tty_create(ttyS0);
    ((tty_t *)ttyS0->device)->ioctl = pl011_tty_ioctl;
    vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), ttyS0);

    tty_t *tty = ttyS0->device;

    struct process *proc = sched_new_process("uart tty", false);
    tty->worker = sched_new_thread(proc, serial_tty_worker_thread, 0, NULL, NULL);
    sched_add_process(proc);
}