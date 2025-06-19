#include <kernel/arch/x86_64/hpet.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>
#include <kernel/mutex.h>
#include <kernel/malloc.h>
#include <kernel/printf.h>
#include <kernel/module.h>
#include <kernel/string.h>

#define AHCI_CAP        0x00    // Host Capabilities
#define AHCI_GHC        0x04    // Global Host Control
#define AHCI_IS         0x08    // Interrupt Status
#define AHCI_PI         0x0C    // Ports Implemented
#define AHCI_VS         0x10    // Version

#define AHCI_DEV_NULL   0
#define AHCI_DEV_SATA   1
#define AHCI_DEV_SATAPI 2
#define AHCI_DEV_SEMB   3
#define AHCI_DEV_PM     4

#define	SATA_SIG_ATA	0x00000101	// SATA drive
#define	SATA_SIG_ATAPI	0xEB140101	// SATAPI drive
#define	SATA_SIG_SEMB	0xC33C0101	// Enclosure management bridge
#define	SATA_SIG_PM	    0x96690101	// Port multiplier

#define GHC_AHCI_ENABLE (1 << 31)   // AHCI Enable
#define GHC_MRSM        (1 << 2)    // MSI Revert to Single Message
#define GHC_IE          (1 << 1)    // Interrupt Enable
#define GHC_HR          (1 << 0)    // HBA Reset

#define HBA_PORT_DET_PRESENT    3
#define HBA_PORT_IPM_ACTIVE     1
#define HBA_CMD_ST      (1 << 0)    // Start
#define HBA_CMD_FRE     (1 << 4)    // FIS Receive Enable
#define HBA_CMD_CR      (1 << 15)   // Command List Running
#define HBA_CMD_FR      (1 << 14)   // FIS Receive Running

#define PORT_CLB        0x00    // Command List Base Address
#define PORT_CLBU       0x04    // Command List Base Address Upper
#define PORT_FB         0x08    // FIS Base Address
#define PORT_FBU        0x0C    // FIS Base Address Upper
#define PORT_IS         0x10    // Interrupt Status
#define PORT_IE         0x14    // Interrupt Enable
#define PORT_CMD        0x18    // Command and Status
#define PORT_TFD        0x20    // Task File Data
#define PORT_SIG        0x24    // Signature
#define PORT_SSTS       0x28    // SATA Status
#define PORT_SCTL       0x2C    // SATA Control
#define PORT_SERR       0x30    // SATA Error
#define PORT_SACT       0x34    // SATA Active
#define PORT_CI         0x38    // Command Issue

#define FIS_TYPE_REG_H2D 0x27

#define LOW(x)  ((uint32_t)(x))
#define HIGH(x) ((uint32_t)((x) >> 32))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
    int port_num;
    void *clb;
    void *fb;
    void *cmd_tbls[32];
    uint64_t clb_phys;
    uint64_t fb_phys;
} ahci_port_t;

typedef struct {
    uint8_t cfl:5;
    uint8_t a:1;
    uint8_t w:1;
    uint8_t p:1;
    
    uint8_t r:1;
    uint8_t b:1;
    uint8_t c:1;
    uint8_t rsv0:1;
    uint8_t pmp:4;
    
    uint16_t prdtl;
    
    volatile uint32_t prdbc;
    
    uint32_t ctba_low;
    uint32_t ctba_high;
    
    uint32_t rsv1[4];
} __attribute__((packed)) hba_cmd_header_t;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t rsv0:3;
    uint8_t c:1;
    uint8_t cmd;
    uint8_t featurel;
    
    /* DWORD 1*/
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    
    /* DWORD 2 */
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    
    /* DWORD 3 */
    uint8_t count_low;
    uint8_t count_high;
    uint8_t icc;
    uint8_t control;
    
    /* DWORD 4 */
    uint8_t rsv1[4];
} __attribute__((packed)) fis_h2d_t;

typedef struct {
    uint32_t dba_low;
    uint32_t dba_high;
    uint32_t rsv0;
    
    uint32_t dbc:22;
    uint32_t rsv1:9;
    uint32_t i:1;
} __attribute__((packed)) hba_prdte_t;

typedef struct {
    uint8_t cmd_fis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    hba_prdte_t entries[1];
} __attribute__((packed)) hba_cmd_tbl_t;

static volatile uint32_t *ahci_base = NULL;
int command_slots = 0, connected_ports = 0;
ahci_port_t *ahci_ports[32];
mutex_t ahci_mutex;

static uint32_t ahci_read_reg(uint32_t offset) {
    return ahci_base[offset / 4];
}

static void ahci_write_reg(uint32_t offset, uint32_t value) {
    ahci_base[offset / 4] = value;
}

static uint32_t port_read_reg(int port, uint32_t offset) {
    uint32_t port_base = 0x100 + (port * 0x80);
    return ahci_read_reg(port_base + offset);
}

static void port_write_reg(int port, uint32_t offset, uint32_t value) {
    uint32_t port_base = 0x100 + (port * 0x80);
    ahci_write_reg(port_base + offset, value);
}

ahci_port_t *ahci_init_disk(int port_num) {
    ahci_port_t *ahci_port = (ahci_port_t *)kmalloc(sizeof(ahci_port_t));
    ahci_port->port_num = port_num;

    uint32_t cmd = port_read_reg(port_num, PORT_CMD);
    cmd &= ~HBA_CMD_ST;
    port_write_reg(port_num, PORT_CMD, cmd);

    int timeout = 500;
    while (timeout-- > 0) {
        cmd = port_read_reg(port_num, PORT_CMD);
        if (!(cmd & HBA_CMD_CR)) {
            break;
        }
        hpet_sleep(1000);
    }

    if (timeout <= 0) {
        dprintf("%s:%d: Failed to stop command engine on port %d\n", __FILE__, __LINE__, port_num);
        kfree(ahci_port);
        return NULL;
    }

    void *clb = VIRTUAL_IDENT(mmu_alloc(1));
    ahci_port->clb_phys = (uint64_t)PHYSICAL_IDENT(clb);
    ahci_port->clb = clb;

    port_write_reg(port_num, PORT_CLB, LOW(ahci_port->clb_phys));
    port_write_reg(port_num, PORT_CLBU, HIGH(ahci_port->clb_phys));
    memset(clb, 0, PAGE_SIZE);

    void *fb = VIRTUAL_IDENT(mmu_alloc(1));
    ahci_port->fb_phys = (uint64_t)PHYSICAL_IDENT(fb);
    ahci_port->fb = fb;

    port_write_reg(port_num, PORT_FB, LOW(ahci_port->fb_phys));
    port_write_reg(port_num, PORT_FBU, HIGH(ahci_port->fb_phys));
    memset(fb, 0, PAGE_SIZE);

    hba_cmd_header_t *cmd_hdr = (hba_cmd_header_t *)clb;
    for (int i = 0; i < 32; i++) {
        cmd_hdr[i].prdtl = 8;

        void *cmd_tbl = VIRTUAL_IDENT(mmu_alloc(1));
        uint64_t cmd_tbl_phys = (uint64_t)PHYSICAL_IDENT(cmd_tbl);
        cmd_hdr[i].ctba_low = LOW(cmd_tbl_phys);
        cmd_hdr[i].ctba_high = HIGH(cmd_tbl_phys);
        memset(cmd_tbl, 0, PAGE_SIZE);

        ahci_port->cmd_tbls[i] = cmd_tbl;
    }

    port_write_reg(port_num, PORT_SERR, 0xFFFFFFFF);

    cmd = port_read_reg(port_num, PORT_CMD);
    cmd |= HBA_CMD_FRE;
    port_write_reg(port_num, PORT_CMD, cmd);

    timeout = 500;
    while (timeout-- > 0) {
        cmd = port_read_reg(port_num, PORT_CMD);
        if (cmd & HBA_CMD_FR) {
            break;
        }
        hpet_sleep(1000);
    }

    cmd |= HBA_CMD_ST;
    port_write_reg(port_num, PORT_CMD, cmd);

    //dprintf("%s:%d: initialized port %d\n", __FILE__, __LINE__, port_num);
    return ahci_port;
}

uint32_t ahci_check_type(int port) {
    uint32_t ssts = port_read_reg(port, PORT_SSTS);
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;
    
    if (det != HBA_PORT_DET_PRESENT)
        return AHCI_DEV_NULL;
    if (ipm != HBA_PORT_IPM_ACTIVE)
        return AHCI_DEV_NULL;
        
    uint32_t sig = port_read_reg(port, PORT_SIG);
    switch (sig) {
        case SATA_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case SATA_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case SATA_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_SATA;
    }
}

int ahci_find_slot(int port_num) {
    uint32_t sact = port_read_reg(port_num, PORT_SACT);
    uint32_t ci = port_read_reg(port_num, PORT_CI);
    uint32_t slots = sact | ci;
    
    for (int i = 0; i < command_slots; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }
    return -1;
}

void ahci_send_cmd(int port_num, uint32_t slot) {
    while (port_read_reg(port_num, PORT_TFD) & 0x88) {
        __asm__ volatile ("pause");
    }
    
    uint32_t cmd = port_read_reg(port_num, PORT_CMD);
    cmd &= ~HBA_CMD_ST;
    port_write_reg(port_num, PORT_CMD, cmd);
    
    while (port_read_reg(port_num, PORT_CMD) & HBA_CMD_CR) {
        __asm__ volatile ("pause");
    }
    
    cmd = port_read_reg(port_num, PORT_CMD);
    cmd |= HBA_CMD_FR | HBA_CMD_ST;
    port_write_reg(port_num, PORT_CMD, cmd);
    
    port_write_reg(port_num, PORT_CI, 1 << slot);
    
    while (port_read_reg(port_num, PORT_CI) & (1 << slot)) {
        __asm__ volatile ("pause");
    }
    
    cmd = port_read_reg(port_num, PORT_CMD);
    cmd &= ~HBA_CMD_ST;
    port_write_reg(port_num, PORT_CMD, cmd);
    
    while (port_read_reg(port_num, PORT_CMD) & HBA_CMD_ST) {
        __asm__ volatile ("pause");
    }
    
    cmd &= ~HBA_CMD_FRE;
    port_write_reg(port_num, PORT_CMD, cmd);
}

int ahci_op(ahci_port_t *ahci_port, uint64_t lba, uint32_t count, char *buffer, bool write) {
    mutex_lock(&ahci_mutex);
    int port_num = ahci_port->port_num;
    
    port_write_reg(port_num, PORT_IS, 0xFFFFFFFF);
    
    int slot = ahci_find_slot(port_num);
    if (slot == -1) {
        return 1;
    }
    
    hba_cmd_header_t *cmd_hdr = (hba_cmd_header_t*)ahci_port->clb;
    cmd_hdr += slot;
    cmd_hdr->cfl = sizeof(fis_h2d_t) / sizeof(uint32_t);
    cmd_hdr->w = write ? 1 : 0;
    cmd_hdr->prdtl = DIV_CEILING(count * 512, PAGE_SIZE);
    
    hba_cmd_tbl_t *cmd_tbl = (hba_cmd_tbl_t*)ahci_port->cmd_tbls[slot];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t) + (cmd_hdr->prdtl - 1) * sizeof(hba_prdte_t));
    
    int32_t remaining_sectors = (int32_t)count;
    for (int i = 0; remaining_sectors > 0; remaining_sectors -= PAGE_SIZE / 512, i++) {
        uint64_t buffer_virt = (uint64_t)buffer + (i * PAGE_SIZE);
        uint64_t page_phys = (uint64_t)PHYSICAL_IDENT((void*)buffer_virt);
        
        cmd_tbl->entries[i].dba_low = LOW(page_phys);
        cmd_tbl->entries[i].dba_high = HIGH(page_phys);
        cmd_tbl->entries[i].dbc = MAX(remaining_sectors * 512 - 1, PAGE_SIZE - 1);
    }
    
    fis_h2d_t *fis_cmd = (fis_h2d_t*)(&cmd_tbl->cmd_fis);
    fis_cmd->fis_type = FIS_TYPE_REG_H2D;
    fis_cmd->c = 1;
    fis_cmd->cmd = write ? 0x35 : 0x25;
    
    fis_cmd->lba0 = (uint8_t)lba;
    fis_cmd->lba1 = (uint8_t)(lba >> 8);
    fis_cmd->lba2 = (uint8_t)(lba >> 16);
    fis_cmd->lba3 = (uint8_t)(lba >> 24);
    fis_cmd->lba4 = (uint8_t)(lba >> 32);
    fis_cmd->lba5 = (uint8_t)(lba >> 40);
    fis_cmd->device = 0x40;
    
    fis_cmd->count_low = (uint8_t)count;
    fis_cmd->count_high = (uint8_t)(count >> 8);
    
    ahci_send_cmd(port_num, slot);
    
    uint32_t is = port_read_reg(port_num, PORT_IS);
    if (is & (1 << 30)) {
        return 1;
    }
    mutex_unlock(&ahci_mutex);
    return 0;
}

int ahci_read(ahci_port_t *ahci_port, uint64_t lba, uint32_t count, char *buffer) {
    return ahci_op(ahci_port, lba, count, buffer, false);
}

long sda_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (len == 0) {
        return -1;
    }

    size_t lba = offset / 512;
    size_t num_sectors = ALIGN_UP(len, 512) / 512;
    size_t pages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
    void *buf = VIRTUAL_IDENT(mmu_alloc(pages));

    if (ahci_read(ahci_ports[0], lba, num_sectors, buf) == 0) {
        memcpy(buffer, buf, len);
        mmu_free(PHYSICAL_IDENT(buf), pages);
        return len;
    } else {
        mmu_free(PHYSICAL_IDENT(buf), pages);
        return 0;
    }
}

int init() {
    dprintf("%s:%d: starting AHCI driver\n", __FILE__, __LINE__);
    mutex_init(&ahci_mutex);

    struct pci_device *ahci_dev = pci_get_device(0x01, 0x06);
    if (!ahci_dev) {
        dprintf("%s:%d: No AHCI controller found!\n", __FILE__, __LINE__);
        return 1;
    }

    uint32_t cmd = pci_read(ahci_dev->bus, ahci_dev->device, 0, 0x04);
    cmd |= (1 << 8) | (1 << 2) | (1 << 1);
    pci_write(ahci_dev->bus, ahci_dev->device, 0, 0x04, cmd);

    uint32_t bar5 = pci_read(ahci_dev->bus, ahci_dev->device, 0, 0x24);
    uint32_t memory_type = bar5 & 0x6;
    uint64_t ahci_phys_base = 0;

    if (memory_type == 0x0) {
        ahci_phys_base = bar5 & 0xFFFFFFF0;
    } else if (memory_type == 0x4) {
        dprintf("%s:%d: FATAL: 64-bit AHCI not implemented\n", __FILE__, __LINE__);
        return 1;
    }

    ahci_base = VIRTUAL(ahci_phys_base);
    mmu_map_pages(2, (void *)ahci_base, (void *)ahci_phys_base, PTE_PRESENT | PTE_WRITABLE); // TODO: PTE_CACHEABLE

    uint32_t ghc = ahci_read_reg(AHCI_GHC);
    ghc |= GHC_HR;
    ahci_write_reg(AHCI_GHC, ghc);

    int i;
    for (i = 0; i < 1000; i++) {
        ghc = ahci_read_reg(AHCI_GHC);
        if (!(ghc & GHC_HR)) {
            break;
        }
        hpet_sleep(1000);
    }

    if (i + 1 >= 1000) {
        dprintf("%s:%d: FATAL: failed to reset controller: timed out\n", __FILE__, __LINE__);
        return 1;
    }

    ghc = ahci_read_reg(AHCI_GHC);
    if (!(ghc & GHC_AHCI_ENABLE)) {
        ghc |= GHC_AHCI_ENABLE;
        hpet_sleep(1000);
        ghc = ahci_read_reg(AHCI_GHC);
        if (!(ghc & GHC_AHCI_ENABLE)) {
            dprintf("%s:%d: FATAL: failed to enable AHCI mode!\n", __FILE__, __LINE__);
            return 1;
        }
    } else {
        //dprintf("%s:%d: AHCI mode already enabled\n", __FILE__, __LINE__);
    }

    uint32_t cap = ahci_read_reg(AHCI_CAP);
    command_slots = ((cap >> 8) & 0x1F) + 1;
    //dprintf("%s:%d: %d command slots available\n", __FILE__, __LINE__, command_slots);

    hpet_sleep(5000);
    ahci_write_reg(AHCI_IS, 0xFFFFFFFF);

    uint32_t pi = ahci_read_reg(AHCI_PI);
    for (int i = 0; i < 32; i++) {
        if (pi & 1) {
            uint32_t type = ahci_check_type(i);
            if (type == AHCI_DEV_SATA) {
                ahci_ports[connected_ports++] = ahci_init_disk(i);
                //dprintf("%s:%d: port %d is a SATA device\n", __FILE__, __LINE__, i);
            }
        }
        pi >>= 1;
    }

    struct vfs_node *sda = vfs_create_node("sda", VFS_BLOCKDEVICE);
    sda->read = sda_read;
    vfs_add_device(sda);

    //printf("\033[92m * \033[97mInitialized AHCI driver\033[0m\n");
    return 0;
}

int fini() {
    dprintf("%s:%d: Goodbye!\n", __FILE__, __LINE__);
    return 0;
}

struct Module metadata = {
    .name = "AHCI driver",
    .init = init,
    .fini = fini
};