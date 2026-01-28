#include <stdbool.h>
#include <stddef.h>
#include <kernel/spinlock.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/time.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>
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
#define PORT_IS         0x10        // Interrupt Status
#define PORT_IE         0x14        // Interrupt Enable
#define PORT_CMD        0x18        // Command and Status
#define PORT_TFD        0x20        // Task File Data
#define PORT_SIG        0x24        // Signature
#define PORT_SSTS       0x28        // SATA Status
#define PORT_SCTL       0x2C        // SATA Control
#define PORT_SERR       0x30        // SATA Error
#define PORT_SACT       0x34        // SATA Active
#define PORT_CI         0x38        // Command Issue

#define FIS_TYPE_REG_H2D 0x27

#define PORT_BASE(port) (0x100 + (port * 0x80))
#define PRDT_ENTRIES    248

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

typedef struct fis_reg_h2d {
	// DWORD 0
	uint8_t  fis_type;	// FIS_TYPE_REG_H2D

	uint8_t  pmport:4;	// Port multiplier
	uint8_t  rsv0:3;	// Reserved
	uint8_t  c:1;		// 1: Command, 0: Control

	uint8_t  command;	// Command register
	uint8_t  featurel;	// Feature register, 7:0
	
	// DWORD 1
	uint8_t  lba0;		// LBA low register, 7:0
	uint8_t  lba1;		// LBA mid register, 15:8
	uint8_t  lba2;		// LBA high register, 23:16
	uint8_t  device;	// Device register

	// DWORD 2
	uint8_t  lba3;		// LBA register, 31:24
	uint8_t  lba4;		// LBA register, 39:32
	uint8_t  lba5;		// LBA register, 47:40
	uint8_t  featureh;	// Feature register, 15:8

	// DWORD 3
	uint8_t  countl;	// Count register, 7:0
	uint8_t  counth;	// Count register, 15:8
	uint8_t  icc;		// Isochronous command completion
	uint8_t  control;	// Control register

	// DWORD 4
	uint8_t  rsv1[4];	// Reserved
} fis_reg_h2d_t;

typedef struct hba_prdt_entry
{
	uint32_t dba;		// Data base address
	uint32_t dbau;		// Data base address upper 32 bits
	uint32_t rsv0;		// Reserved

	// DW3
	uint32_t dbc:22;	// Byte count, 4M max
	uint32_t rsv1:9;	// Reserved
	uint32_t i:1;		// Interrupt on completion
} hba_prdt_entry_t;

typedef struct hba_cmd_tbl {
	// 0x00
	uint8_t  cfis[64];	// Command FIS

	// 0x40
	uint8_t  acmd[16];	// ATAPI command, 12 or 16 bytes

	// 0x50
	uint8_t  rsv[48];	// Reserved

	// 0x80
	hba_prdt_entry_t	prdt_entry[0];	// Physical region descriptor table entries, 0 ~ 65535
} hba_cmd_tbl_t;

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
spinlock_t ahci_lock[32] = {0};

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
    
    // Reset the port.
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

    // Allocate physical memory for its command list
    port->clb_phys = (uint64_t)mmu_alloc();
    port->clb = VIRTUAL_HHDM(port->clb_phys);   // TODO: Memory map these as uncacheable.

    // Set command list registers (and upper registers, if supported).
    ahci_write(PORT_BASE(port_num) + PORT_CLB, LOW(port->clb_phys));
    ahci_write(PORT_BASE(port_num) + PORT_CLBU, HIGH(port->clb_phys));
    memset(port->clb, 0, PAGE_SIZE);

    // Allocate physical memory for the received FIS
    port->fb_phys = (uint64_t)mmu_alloc();
    port->fb = VIRTUAL_HHDM(port->fb_phys);
    
    // Set received FIS address registers (and upper registers, if supported).
    ahci_write(PORT_BASE(port_num) + PORT_FB, LOW(port->fb_phys));
    ahci_write(PORT_BASE(port_num) + PORT_FBU, HIGH(port->fb_phys));
    memset(port->fb, 0, PAGE_SIZE);

    hba_cmd_header_t *cmd_hdr = (hba_cmd_header_t *)port->clb;
    for (i = 0; i < 32; i++) {
        cmd_hdr[i].prdtl = PRDT_ENTRIES; // ~992KB

        // Allocate physical memory for its command tables
        uintptr_t phys = (uintptr_t)mmu_alloc();
        cmd_hdr[i].ctba = LOW(phys);
        cmd_hdr[i].ctbau = HIGH(phys);
        memset(VIRTUAL_HHDM(phys), 0, PAGE_SIZE);

        // Setup command list entries to point to the corresponding command table.
        port->cmd_tbls[i] = VIRTUAL_HHDM(phys);
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

    // Start command list processing with the port's command register.
    ahci_write(PORT_BASE(port_num) + PORT_CMD, ahci_read(PORT_BASE(port_num) + PORT_CMD) | HBA_CMD_ST);
    
    // TODO: Read signature/status of the port to see if it connected to a drive.
    // TODO: Send IDENTIFY ATA command to connected drives. Get their sector size and count.

    dprintf(LOG_DEBUG, "\033[93mahci:\033[0m initialized port %d\n", port_num);
    return port;
}

void ahci_send_cmd(int port, uint32_t slot) {
    while (ahci_read(PORT_BASE(port) + PORT_TFD) & 0x88) {
        #ifdef __x86_64__
        asm volatile ("pause");
        #endif
    }
    ahci_write(PORT_BASE(port) + PORT_CI, 1 << slot);
    while (ahci_read(PORT_BASE(port) + PORT_CI) & (1 << slot)) {
        #ifdef __x86_64__
        asm volatile ("pause");
        #endif
    }
}

int ahci_find_slot(int port) {
    uint32_t sact = ahci_read(PORT_BASE(port) + PORT_SACT);
    uint32_t ci = ahci_read(PORT_BASE(port) + PORT_CI);
    uint32_t slots = sact | ci;
    
    for (int i = 0; i < command_slots; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }
    return -1;
}

int ahci_op(ahci_port_t *port, uint64_t lba, uint32_t count, void *buffer, bool write) {
    if (DIV_CEILING(count, 8) > PRDT_ENTRIES) {
        dprintf(LOG_ERR, "\033[93mahci:\033[0m transfer too large (%u)\n", count);
        release(&ahci_lock[port->port_num]);
        return 1;
    }

    acquire(&ahci_lock[port->port_num]);
    ahci_write(PORT_BASE(port->port_num) + PORT_IS, 0xFFFFFFFF);
    int slot = ahci_find_slot(port->port_num);
    if (slot < 0) {
        release(&ahci_lock[port->port_num]);
        return 1;
    }
    
    hba_cmd_header_t *cmd_hdr = port->clb;
    cmd_hdr += slot;
    cmd_hdr->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmd_hdr->w = write;
    cmd_hdr->prdtl = DIV_CEILING(count, 8);
    
    hba_cmd_tbl_t *cmd_tbl = port->cmd_tbls[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + (cmd_hdr->prdtl - 1) * sizeof(hba_prdt_entry_t));
    
    uintptr_t *pages = kmalloc(sizeof(uintptr_t) * ALIGN_UP(count, 8) / 8);
    for (uint32_t i = 0; i < ALIGN_UP(count, 8) / 8; i++) {
        pages[i] = (uintptr_t)mmu_alloc();
        if (write)
            memcpy(VIRTUAL_HHDM(pages[i]), buffer + i * PAGE_SIZE, MIN((count - i * 8) * 512, PAGE_SIZE));
    }
    
    for (int i = 0, j = count; j > 0; j -= 8, i++) {
        uintptr_t phys = pages[i];
        cmd_tbl->prdt_entry[i].dba = LOW(phys);
        cmd_tbl->prdt_entry[i].dbau = HIGH(phys);
        cmd_tbl->prdt_entry[i].dbc = MIN(j * 512, PAGE_SIZE) - 1;
    }
    
    fis_reg_h2d_t *fis_cmd = (fis_reg_h2d_t *)(&cmd_tbl->cfis);
    fis_cmd->fis_type = FIS_TYPE_REG_H2D;
    fis_cmd->c = 1;
    fis_cmd->command = write ? 0x35 : 0x25;
    fis_cmd->lba0 = (uint8_t)lba;
    fis_cmd->lba1 = (uint8_t)(lba >> 8);
    fis_cmd->lba2 = (uint8_t)(lba >> 16);
    fis_cmd->lba3 = (uint8_t)(lba >> 24);
    fis_cmd->lba4 = (uint8_t)(lba >> 32);
    fis_cmd->lba5 = (uint8_t)(lba >> 40);
    fis_cmd->device = 0x40;
    fis_cmd->countl = (uint8_t)count;
    fis_cmd->counth = (uint8_t)(count >> 8);
    
    ahci_send_cmd(port->port_num, slot);
    release(&ahci_lock[port->port_num]);

    int status = ahci_read(PORT_BASE(port->port_num) + PORT_IS) & (1 << 30);
    
    for (uint32_t i = 0; i < ALIGN_UP(count, 8) / 8; i++) {
        if (!write && !status)
            memcpy(buffer + i * PAGE_SIZE, VIRTUAL_HHDM(pages[i]), MIN((count - i * 8) * 512, PAGE_SIZE));
        mmu_free((void *)pages[i]);
    }
    kfree(pages);
    return status;
}

int ahci_op_read(ahci_port_t *port, uint64_t lba, uint32_t count, char *buffer) {
    return ahci_op(port, lba, count, buffer, false);
}

int ahci_op_write(ahci_port_t *port, uint64_t lba, uint32_t count, const char *buffer) {
    return ahci_op(port, lba, count, (char *)buffer, true);
}

uint32_t ahci_get_type(int port) {
    uint32_t ssts = ahci_read(PORT_BASE(port) + PORT_SSTS);
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    if (det != HBA_PORT_DET_PRESENT || ipm != HBA_PORT_IPM_ACTIVE)
        return 0;

    // Reset the port.
    uint32_t cmd = ahci_read(PORT_BASE(port) + PORT_CMD);
    if (!(cmd & HBA_CMD_FRE)) {
        ahci_write(PORT_BASE(port) + PORT_CMD, cmd | HBA_CMD_FRE);
        arch_sleep(10000000UL);
    }
    
    return ahci_read(PORT_BASE(port) + PORT_SIG);
}

long ahci_vfs_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    if (!len)
        return 0;

    size_t lba = offset / 512;
    size_t count = ALIGN_DOWN(len, 512) / 512;

    if (ahci_op_read(node->device, lba, count, buffer) < 0)
        return -EIO;
    return len;
}

long ahci_vfs_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    if (!len)
        return 0;

    size_t lba = offset / 512;
    size_t count = ALIGN_DOWN(len, 512) / 512;

    if (ahci_op_write(node->device, lba, count, buffer) < 0)
        return -EIO;
    return len;
}

vfs_ops_t ops = {
    .read = ahci_vfs_read,
    .write = ahci_vfs_write
};

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
    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif
    for (size_t i = 0; i < 2 * PAGE_SIZE; i += PAGE_SIZE) {
        mmu_map(kernel_pd, (void *)(ahci_base + i), (void *)(phys_base + i), flags);
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
    dprintf(LOG_DEBUG, "\033[93mahci:\033[0m reset controller\n");

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
    dprintf(LOG_DEBUG, "\033[93mahci:\033[0m 64-bit DMA %s supported\n", cap & (1u << 31) ? "is" : "is not");

    arch_sleep(5000000UL);
    ahci_write(AHCI_IS, 0xFFFFFFFF);

    uint32_t pi = ahci_read(AHCI_PI);
    for (i = 0; i < 32; i++) {
        if (bitmap_get((uint8_t *)&pi, i)) {
            switch (ahci_get_type(i)) {
                case SATA_SIG_ATA: {
                    vfs_node_t *node = devfs_create_numbered(DEVFS_SSD);
                    node->perms = 0660;
                    node->ops = &ops;
                    node->device = ahci_init_drive(i);
                    break;
                }
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