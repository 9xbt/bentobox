#include <errno.h>
#include <kernel/malloc.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/args.h>
#include <kernel/vfs.h>

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} __attribute__((packed)) guid_t;

typedef struct {
    char signature[8];
    uint32_t revision;
    uint32_t size;
    uint32_t gpt_crc;
    uint32_t reserved;
    uint64_t lba;
    uint64_t alternate_lba;
    uint64_t first_usable_block;
    uint64_t last_usable_block;
    guid_t   guid;
    uint64_t pe_lba;
    uint32_t pe_num;
    uint32_t pe_size;
    uint32_t pe_crc;
} partition_table_t;

typedef struct {
    guid_t type_guid;
    guid_t guid;
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    short name[36];
} partition_entry_t;

typedef struct {
    struct vfs_node *drive;
    uint64_t offset;
} partition_device_t;

long gpt_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node->device)
        return -EIO;
    partition_device_t *device = node->device;
    return vfs_read(device->drive, buffer, device->offset + offset, len);
}

long gpt_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node->device)
        return -EIO;
    partition_device_t *device = node->device;
    return vfs_write(device->drive, buffer, device->offset + offset, len);
}

int init() {
    int drive_num = 0;
    char drive_name[] = "/dev/sda";
    struct vfs_node *drive = vfs_open(NULL, drive_name, false, false);
    do {
        if (!drive) {
            dprintf(6, "%s:%d: cannot open %s\n", __FILE__, __LINE__, "/dev/sda");
            return -EINVAL;
        }

        partition_table_t *pt = kmalloc(sizeof(partition_table_t));
        vfs_read(drive, pt, 512, sizeof(partition_table_t));

        if (strcmp(pt->signature, "EFI PART")) {
            dprintf(6, "%s:%d: not a GPT partition table\n", __FILE__, __LINE__);
            return -EINVAL;
        }

        dprintf(6, "%s:%d: GPT signature: %s\n", __FILE__, __LINE__, pt->signature);
        dprintf(6, "%s:%d: %d paritition slots\n", __FILE__, __LINE__, pt->pe_num);

        partition_entry_t *pe = kmalloc(pt->pe_size * pt->pe_num);
        vfs_read(drive, pe, pt->pe_lba * 512, pt->pe_size * pt->pe_num);

        struct vfs_node *part;
        partition_device_t *device;
        char mountpoint[16] = {0};
        for (uint32_t i = 0; i < pt->pe_num; i++) {
            if (pe[i].type_guid.data1 == 0) {
                continue;
            }
            sprintf(mountpoint, "sda%d", i + 1);

            dprintf(6, "%s:%d: creating mountpoint for '%ls' at /dev/%s\n", __FILE__, __LINE__, pe[i].name, mountpoint);
            device = kmalloc(sizeof(partition_device_t));
            device->drive = drive;
            device->offset = pe[i].start_lba * 512;
            part = vfs_create_node(mountpoint, VFS_BLOCKDEVICE);
            part->perms = 0660;
            part->read = gpt_read;
            part->write = gpt_write;
            part->device = device;
            vfs_add_device(part);
        }

        kfree(pt);
        kfree(pe);

        sprintf(drive_name, "/dev/sd%c", 'a' + ++drive_num);
    } while ((drive = vfs_open(NULL, drive_name, false, false)) != NULL);
    return 0;
}

int fini() {
    dprintf(6, "%s:%d: Goodbye!\n", __FILE__, __LINE__);
    return 0;
}

struct Module metadata = {
    .name = "GPT module",
    .init = init,
    .fini = fini
};