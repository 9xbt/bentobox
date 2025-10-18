#include <stdint.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/spinlock.h>
#include <kernel/string.h>
#include <kernel/printf.h>
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

volatile uint32_t *pl011_base = NULL;
spinlock_t uart_lock = 0;
vfs_node_t *tty;

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

void pl011_irq_handler(struct registers *r) {
    (void)r;
    char c = pl011_read(UARTDR) & 0xFF;
    tty->tty_ops->enqueue(tty, c);
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

    tty = vfs_lookup(NULL, "/dev/tty1", true, VFS_NONE);
}