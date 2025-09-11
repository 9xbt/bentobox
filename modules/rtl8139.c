#include <stddef.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <kernel/pci.h>

#define MAC(x)      (0x00 + x)
#define RBSTART     0x30
#define CMD         0x37
#define IMR         0x3C
#define TCR         0x44
#define CONFIG_1    0x52

#define CR_RESET    (1 << 4)
#define CR_RE       (1 << 2)
#define CR_TE       (1 << 3)
#define CR_BUFE     (1 << 5)
#define CR_STA      (1 << 6)
#define CR_LOOPBACK (1 << 7)

#define ISR_ROK     (1 << 0)
#define ISR_TOK     (1 << 2)
#define ISR_RXERR   (1 << 1)
#define ISR_TXERR   (1 << 3)
#define ISR_RER     (1 << 6)
#define ISR_PUN     (1 << 7)
#define ISR_TER     (1 << 8)
#define ISR_RXOVW   (1 << 9)
#define ISR_CNT     (1 << 10)

#define TCR_CRC_ENABLE          (1 << 0)
#define TCR_PAD_SHORT_FRAMES    (1 << 1)
#define TCR_COLLISION_TEST      (1 << 2)
#define TCR_AUTO_DEFER          (1 << 3)
#define TCR_LOOPBACK            (1 << 7)

uint32_t ioaddr = 0;
uint8_t mac[6] = {0};
void *rx_buffer = NULL;

void rtl8139_irq_handler(struct registers *r) {
    dprintf(LOG_INFO, "rtl8139 irq\n");
    lapic_eoi();
}

int init() {
    struct pci_device *rtl_dev = pci_get_device_from_vendor(0x10ec, 0x8139);
    if (!rtl_dev) {
        dprintf(LOG_ERR, "%s:%d: No RTL8139 NIC found!\n", __FILE__, __LINE__);
        return 1;
    }

    pci_write(rtl_dev->bus, rtl_dev->device, rtl_dev->function, 0x04,
        pci_read(rtl_dev->bus, rtl_dev->device, rtl_dev->function, 0x04) | PCI_BUS_MASTER);

    ioaddr = pci_read(rtl_dev->bus, rtl_dev->device, rtl_dev->function, 0x10) & 0xFFFFFFFC;

    outb(ioaddr + CONFIG_1, 0x00);
    outb(ioaddr + CMD, 0x10);
    while (inb(ioaddr + CMD) & 0x10);
    rx_buffer = mmu_alloc(ALIGN_UP(8192 + 16 + 1500, PAGE_SIZE) / PAGE_SIZE);
    outl(ioaddr + RBSTART, (uint32_t)(uintptr_t)rx_buffer);
    outw(ioaddr + 0x3C, ISR_TOK | ISR_ROK);
    outl(ioaddr + TCR, TCR_CRC_ENABLE | TCR_PAD_SHORT_FRAMES | TCR_COLLISION_TEST | TCR_AUTO_DEFER | TCR_LOOPBACK);
    outb(ioaddr + CMD, CR_RE | CR_TE);

    mac[0] = inb(ioaddr + MAC(0));
    mac[1] = inb(ioaddr + MAC(1));
    mac[2] = inb(ioaddr + MAC(2));
    mac[3] = inb(ioaddr + MAC(3));
    mac[4] = inb(ioaddr + MAC(4));
    mac[5] = inb(ioaddr + MAC(5));

    dprintf(LOG_INFO, "%s:%d: MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", __FILE__, __LINE__, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    uint8_t irq = pci_read(rtl_dev->bus, rtl_dev->device, rtl_dev->function, 0x3C) & 0xFF;
    dprintf(LOG_INFO, "%s:%d: registering handler for IRQ %u\n", __FILE__, __LINE__, irq);
    irq_register(irq, rtl8139_irq_handler);
    return 0;
}

int fini() {
    dprintf(LOG_INFO, "%s:%d: Goodbye!\n", __FILE__, __LINE__);
    return 0;
}

struct Module metadata = {
    .name = "RTL8139 driver",
    .init = init,
    .fini = fini
};