#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>

#define AHCI_CAP        0x00        // Host Capabilities
#define AHCI_GHC        0x04        // Global Host Control
#define AHCI_IS         0x08        // Interrupt Status
#define AHCI_PI         0x0C        // Ports Implemented
#define AHCI_VS         0x10        // Version

#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	    0x96690101	// Port multiplier

#define GHC_AHCI_ENABLE (1u << 31)  // AHCI Enable
#define GHC_HR          (1 << 0)    // HBA Reset

#define HBA_PORT_DET_PRESENT    3
#define HBA_PORT_IPM_ACTIVE     1
#define HBA_CMD_ST      (1 << 0)    // Start
#define HBA_CMD_FRE     (1 << 4)    // FIS Receive Enable
#define HBA_CMD_CR      (1 << 15)   // Command List Running
#define HBA_CMD_FR      (1 << 14)   // FIS Receive Running

#define PORT_CLB        0x00        // Command List Base Address
#define PORT_CLBU       0x04        // Command List Base Address Upper
#define PORT_FB         0x08        // FIS Base Address
#define PORT_FBU        0x0C        // FIS Base Address Upper
#define PORT_CMD        0x18        // Command and Status
#define PORT_SIG        0x24        // Signature
#define PORT_SSTS       0x28        // SATA Status
#define PORT_SERR       0x30        // SATA Error

#define PORT_BASE(port) (0x100 + (port * 0x80))

typedef struct hba_cmd_header {
	// DW0
	uint8_t  cfl:5;		// Command FIS length in DWORDS, 2 ~ 16
	uint8_t  a:1;		// ATAPI
	uint8_t  w:1;		// Write, 1: H2D, 0: D2H
	uint8_t  p:1;		// Prefetchable

	uint8_t  r:1;		// Reset
	uint8_t  b:1;		// BIST
	uint8_t  c:1;		// Clear busy upon R_OK
	uint8_t  rsv0:1;	// Reserved
	uint8_t  pmp:4;		// Port multiplier port

	uint16_t prdtl;		// Physical region descriptor table length in entries

	// DW1
	volatile
    uint32_t prdbc;     // Physical region descriptor byte count transferred

	// DW2, 3
	uint32_t ctba;		// Command table descriptor base address
	uint32_t ctbau;		// Command table descriptor base address upper 32 bits

	// DW4 - 7
	uint32_t rsv1[4];	// Reserved
} hba_cmd_header_t;

typedef struct ahci_port {
    int port_num;
    void *clb;
    void *fb;
    void *cmd_tbls[32];
    uintptr_t clb_phys;
    uintptr_t fb_phys;
} ahci_port_t;

volatile uint32_t *ahci_base = NULL;
int command_slots = 0;

uint32_t ahci_read(uint32_t offset) {
    return ahci_base[offset / sizeof(uint32_t)];
}

void ahci_write(uint32_t offset, uint32_t value) {
    ahci_base[offset / sizeof(uint32_t)] = value;
}

ahci_port_t *ahci_init_drive(int port_num) {
    ahci_port_t *port = kmalloc(sizeof(ahci_port_t));
    port->port_num = port_num;

    ahci_write(PORT_BASE(port_num) + PORT_CMD, ahci_read(PORT_BASE(port_num) + PORT_CMD) & ~HBA_CMD_ST);
    
    int i;
    for (i = 0; i < 500; i++) {
        if (!(ahci_read(PORT_BASE(port_num) + PORT_CMD) & HBA_CMD_CR))
            break;
        arch_sleep(1000000UL);
    }
    if (i >= 500) {
        dprintf(LOG_ERR, "\033[93mahci:\033[0m failed to stop command engine on port %d\n", port_num);
        kfree(port);
        return NULL;
    }

    port->clb_phys = (uint64_t)mmu_alloc();
    port->clb = VIRTUAL_HHDM(port->clb_phys);

    ahci_write(PORT_BASE(port_num) + PORT_CLB, LOW(port->clb_phys));
    ahci_write(PORT_BASE(port_num) + PORT_CLBU, HIGH(port->clb_phys));
    memset(port->clb, 0, PAGE_SIZE);

    port->fb_phys = (uint64_t)mmu_alloc();
    port->fb = VIRTUAL_HHDM(port->fb_phys);
    
    ahci_write(PORT_BASE(port_num) + PORT_FB, LOW(port->fb_phys));
    ahci_write(PORT_BASE(port_num) + PORT_FBU, HIGH(port->fb_phys));
    memset(port->fb, 0, PAGE_SIZE);

    hba_cmd_header_t *cmd_hdr = (hba_cmd_header_t *)port->clb;
    for (i = 0; i < 32; i++) {
        cmd_hdr[i].prdtl = 8;

        uintptr_t cmd_tbl_phys = (uintptr_t)mmu_alloc();
        void *cmd_tbl = VIRTUAL_HHDM(cmd_tbl_phys);
        cmd_hdr[i].ctba = LOW(cmd_tbl_phys);
        cmd_hdr[i].ctbau = HIGH(cmd_tbl_phys);
        memset(cmd_tbl, 0, PAGE_SIZE);

        port->cmd_tbls[i] = cmd_tbl;
    }

    ahci_write(PORT_BASE(port_num) + PORT_SERR, 0xFFFFFFFF);
    ahci_write(PORT_BASE(port_num) + PORT_CMD, ahci_read(PORT_BASE(port_num) + PORT_CMD) | HBA_CMD_FRE);

    for (i = 0; i < 500; i++) {
        if (ahci_read(PORT_BASE(port_num) + PORT_CMD) & HBA_CMD_FR)
            break;
        arch_sleep(1000000UL);
    }
    if (i >= 500) {
        dprintf(LOG_ERR, "\033[93mahci:\033[0m timed out while waiting for FIS receive engine to start on port %d\n", port_num);
        kfree(port);
        return NULL;
    }

    ahci_write(PORT_BASE(port_num) + PORT_CMD, ahci_read(PORT_BASE(port_num) + PORT_CMD) | HBA_CMD_ST);

    dprintf(LOG_DEBUG, "\033[93mahci:\033[0m initialized port %d\n", port_num);
    return port;
}

uint32_t ahci_get_type(int port) {
    uint32_t ssts = ahci_read(PORT_BASE(port) + PORT_SSTS);
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE)
        return 0;

    uint32_t cmd = ahci_read(PORT_BASE(port) + PORT_CMD);
    if (!(cmd & HBA_CMD_FRE)) {
        ahci_write(PORT_BASE(port) + PORT_CMD, cmd | HBA_CMD_FRE);
        arch_sleep(10000000UL);
    }
    
    return ahci_read(PORT_BASE(port) + PORT_SIG);
}

int init() {
    pci_device_t *ahci = pci_get_device(0x01, 0x06);
    if (!ahci) {
        dprintf(LOG_INFO, "\033[93mahci:\033[0m no controllers found\n");
        return 1;
    }

    // Enable interrupts, DMA, and memory space access in the PCI command register
    uint32_t cmd = pci_config_read_word(ahci->bus, ahci->device, ahci->function, 0x04);
    cmd |= PCI_SERR_ENABLE | PCI_BUS_MASTER | PCI_IO_SPACE;
    pci_config_write_word(ahci->bus, ahci->device, ahci->function, 0x04, cmd);

    uint32_t bar5 = pci_read(ahci->bus, ahci->device, ahci->function, 0x24);
    if ((bar5 & 0x6) != 0x0) {
        dprintf(LOG_ERR, "\033[93mahci:\033[0m: 64-bit AHCI not implemented\n");
        return 1;
    }
    uint32_t phys_base = bar5 & 0xFFFFFFF0;

    // Memory map BAR 5 register as uncacheable.
    ahci_base = VIRTUAL_HHDM(phys_base);
    for (size_t i = 0; i < 2 * PAGE_SIZE; i += PAGE_SIZE) {
        mmu_map(kernel_pd, (void *)(ahci_base + i), (void *)(phys_base + i), PTE_PRESENT | PTE_WRITABLE);
    }

    // TODO: Perform BIOS/OS handoff (if the bit in the extended capabilities is set)

    // Reset controller
    ahci_write(AHCI_GHC, ahci_read(AHCI_GHC) | GHC_HR);

    int i;
    for (i = 0; i < 1000; i++) {
        if (!(ahci_read(AHCI_GHC) & GHC_HR))
            break;
        arch_sleep(1000000UL);
    }
    if (i >= 1000) {
        dprintf(LOG_ERR, "\033[93mahci:\033[0m failed to reset controller: timed out\n");
        return 1;
    }
    dprintf(LOG_DEBUG, "\033[93mahci:\033[0m controller reset\n");

    // Enable AHCI mode and interrupts in global host control register.
    if (!(ahci_read(AHCI_GHC) & GHC_AHCI_ENABLE)) {
        ahci_write(AHCI_GHC, ahci_read(AHCI_GHC) | GHC_AHCI_ENABLE);
        arch_sleep(1000000UL);
        if (!(ahci_read(AHCI_GHC) & GHC_AHCI_ENABLE)) {
            dprintf(LOG_ERR, "\033[93mahci:\033[0m failed to enable AHCI mode\n\n", __FILE__, __LINE__);
            return 1;
        }
    } else {
        dprintf(LOG_DEBUG, "\033[93mahci:\033[0m AHCI mode already enabled\n");
    }

    // Read capabilities registers. Check 64-bit DMA is supported if you need it.
    uint32_t cap = ahci_read(AHCI_CAP);
    command_slots = ((cap >> 8) & 0x1F) + 1;
    dprintf(LOG_DEBUG, "\033[93mahci:\033[0m %d command slots available\n", command_slots);

    arch_sleep(5000000UL);
    ahci_write(AHCI_IS, 0xFFFFFFFF);

    uint32_t pi = ahci_read(AHCI_PI);
    for (i = 0; i < 32; i++) {
        if (bitmap_get((uint8_t *)&pi, i)) {
            switch (ahci_get_type(i)) {
                case SATA_SIG_ATA:
                    ahci_init_drive(i);
                    break;
            }
        }
    }

    dprintf(LOG_INFO, "\033[93mahci:\033[0m initialized AHCI driver\n");
    return 0;
}

int fini() {
    return 0;
}

struct Module metadata = {
    .name = "ahci",
    .init = init,
    .fini = fini
};