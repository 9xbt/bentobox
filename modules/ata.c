#include <kernel/arch/x86_64/io.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mutex.h>
#include <kernel/mmu.h>
#include <kernel/pci.h>

#define ATA_PRIMARY         0x1F0
#define ATA_SECONDARY       0x170
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_CTRL  0x376
#define ATA_MASTER          0xA0
#define ATA_SLAVE           0xB0
#define ATA_WAIT            0x00
#define ATA_IDENTIFY        0xEC
#define ATA_READ            0x20
#define ATA_WRITE           0x30
#define ATA_OK              0x00
#define ATA_NO_DRIVES       0x01
#define ATA_DISK_ERR        0x02
#define ATA_IS_SATA         0x03

char *ata_errors[] = {
    "",
    "no drives",
    "disk error",
    "drive is actually SATA"
};

uint16_t ata_base;
uint8_t  ata_type;
uint8_t *ata_ident = NULL;
mutex_t  ata_mutex;

void ata_400ns(void) {
    for (int i = 0; i < 4; i++) {
        inb(ata_base + 7);
    }
}

uint8_t ata_poll() {
    ata_400ns();

    uint8_t status = 0;
    for (;;) {
        status = inb(ata_base + 7);
        if (!(status & 0x80)) {
            break;
        } 
        if (status & 0x08) {
            break;
        }
        if (status & 0x01) {
            return ATA_DISK_ERR;
        }
    }

    return ATA_OK;
}

__attribute__((no_sanitize("undefined")))
uint8_t ata_read(uint32_t lba, void *buffer, uint32_t sectors) {
    mutex_lock(&ata_mutex);
    
    for (uint32_t offset = 0; offset < sectors; offset += 255) {
        uint32_t chunk_size = ((sectors - offset) > 255) ? 255 : (sectors - offset);
        uint32_t current_lba = lba + offset;
        
        outb(ata_base + 6, (ata_type == ATA_MASTER ? 0xE0 : 0xF0) | ((current_lba >> 24) & 0x0F));
        outb(ata_base + 1, ATA_WAIT);
        outb(ata_base + 2, (uint8_t)chunk_size);
        outb(ata_base + 3, (uint8_t)current_lba);
        outb(ata_base + 4, (uint8_t)(current_lba >> 8));
        outb(ata_base + 5, (uint8_t)(current_lba >> 16));
        outb(ata_base + 7, ATA_READ);
        
        uint16_t *buf = (uint16_t *)buffer + (offset * 256);
        for (uint32_t i = 0; i < chunk_size * 256; i++) {
            if (ata_poll() != ATA_OK) {
                mutex_unlock(&ata_mutex);
                return ATA_DISK_ERR;
            }
            buf[i] = inw(ata_base);
        }
        
        ata_400ns();
    }
    
    mutex_unlock(&ata_mutex);
    return ATA_OK;
}

__attribute__((no_sanitize("undefined")))
uint8_t ata_write(uint32_t lba, void *buffer, uint32_t sectors) {
    mutex_lock(&ata_mutex);
    
    for (uint32_t offset = 0; offset < sectors; offset += 255) {
        uint32_t chunk_size = ((sectors - offset) > 255) ? 255 : (sectors - offset);
        uint32_t current_lba = lba + offset;
        
        outb(ata_base + 6, (ata_type == ATA_MASTER ? 0xE0 : 0xF0) | ((current_lba >> 24) & 0x0F));
        outb(ata_base + 1, ATA_WAIT);
        outb(ata_base + 2, (uint8_t)chunk_size);
        outb(ata_base + 3, (uint8_t)current_lba);
        outb(ata_base + 4, (uint8_t)(current_lba >> 8));
        outb(ata_base + 5, (uint8_t)(current_lba >> 16));
        outb(ata_base + 7, ATA_WRITE);
        
        uint16_t *buf = (uint16_t *)buffer + (offset * 256);
        for (uint32_t i = 0; i < chunk_size * 256; i++) {
            if (ata_poll() != ATA_OK) {
                mutex_unlock(&ata_mutex);
                return ATA_DISK_ERR;
            }
            outw(ata_base, buf[i]);
        }
        
        ata_400ns();
    }
    
    mutex_unlock(&ata_mutex);
    return ATA_OK;
}

uint8_t ata_identify(uint16_t base, uint8_t type) {
    ata_base = base;
    ata_type = type;

    outb(base + 6, type);
    ata_400ns();
    
    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    
    outb(base + 7, ATA_IDENTIFY);

    uint8_t status = inb(base + 7);
    if (!status) {
        return ATA_NO_DRIVES;
    }
    
    if (ata_poll() != ATA_OK) {
        return ATA_DISK_ERR;
    }

    ata_ident = kmalloc(512);
    
    uint16_t *identify_data = (uint16_t *)ata_ident;
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(base);
    }

    uint16_t transport = identify_data[222];

    if (transport & 0x02) {
        kfree(ata_ident);
        return ATA_IS_SATA;
    }

    char name[41];

    uint8_t i = 0;
    for (i = 0; i < 40; i += 2) {
        name[i] = ata_ident[54 + i + 1];
        name[i + 1] = ata_ident[54 + i];
    }
    
    for (i = 39; i > 0; i--) {
        if (name[i] != ' ') {
            name[i + 1] = 0;
            break;
        }
    }
    dprintf(LOG_INFO, "%s:%d: drive name: '%s'\n", __FILE__, __LINE__, name);

    ata_400ns();
    return ATA_OK;
}

long hda_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (len == 0) return 0;

    size_t lba = offset / 512;
    size_t num_sectors = ALIGN_UP(len, 512) / 512;
    size_t pages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
    void *buf = VIRTUAL_IDENT(mmu_alloc(pages));

    if (offset % 512 != 0 || len % 512 != 0) {
        if (ata_read(lba, buf, num_sectors) != ATA_OK) {
            mmu_free(PHYSICAL_IDENT(buf), pages);
            return 0;
        }
        memcpy((uint8_t*)buf + (offset % 512), buffer, len);
    } else {
        memcpy(buf, buffer, len);
    }

    if (ata_write(lba, buf, num_sectors) == ATA_OK) {
        mmu_free(PHYSICAL_IDENT(buf), pages);
        return len;
    } else {
        mmu_free(PHYSICAL_IDENT(buf), pages);
        return 0;
    }
}

long hda_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (len == 0) return 0;

    size_t lba = offset / 512;
    size_t num_sectors = ALIGN_UP(len, 512) / 512;
    size_t pages = ALIGN_UP(len, PAGE_SIZE) / PAGE_SIZE;
    void *buf = VIRTUAL_IDENT(mmu_alloc(pages));

    if (ata_read(lba, buf, num_sectors) == ATA_OK) {
        memcpy(buffer, buf, len);
        mmu_free(PHYSICAL_IDENT(buf), pages);
        return len;
    } else {
        mmu_free(PHYSICAL_IDENT(buf), pages);
        return 0;
    }
}

int init() {
    dprintf(LOG_INFO, "%s:%d: starting ATA driver\n", __FILE__, __LINE__);

    mutex_init(&ata_mutex);

    uint8_t err = ata_identify(ATA_PRIMARY, ATA_MASTER);
    if (err != ATA_OK) {
        dprintf(LOG_ERR, "%s:%d: failed to initialize: %s\n", __FILE__, __LINE__, ata_errors[err]);
        return 1;
    }

    struct vfs_node *hda = vfs_create_node("sda", VFS_BLOCKDEVICE);
    hda->read = hda_read;
    hda->write = hda_write;
    vfs_add_device(hda);
    return 0;
}

int fini() {
    dprintf(LOG_INFO, "%s:%d: Goodbye!\n", __FILE__, __LINE__);
    kfree(ata_ident);
    return 0;
}

struct Module metadata = {
    .name = "ATA driver",
    .init = init,
    .fini = fini
};