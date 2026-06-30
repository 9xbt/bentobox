#include <kernel/assert.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/virtio.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/pci.h>
#include <kernel/mmu.h>

list_t *virtio_devices = NULL;

struct virtq *virtio_setup_virtqueue(struct virtio_device *viodev, uint16_t index) {
    viodev->common_cfg->queue_select = index;
    uint16_t num = viodev->common_cfg->queue_size;
    assert(16 * num <= 4096);
    dprintf(LOG_DEBUG, "\033[93mvirtio:\033[0m virtq num %u\n", num);

    struct virtq_desc *desc = VIRTUAL_HHDM(mmu_alloc());
    struct virtq_avail *avail = VIRTUAL_HHDM(mmu_alloc());
    struct virtq_used *used = VIRTUAL_HHDM(mmu_alloc());
    memset(desc, 0, PAGE_SIZE);
    memset(avail, 0, PAGE_SIZE);
    memset(used, 0, PAGE_SIZE);

    viodev->common_cfg->queue_desc = (uint64_t)PHYSICAL_HHDM(desc);
    viodev->common_cfg->queue_driver = (uint64_t)PHYSICAL_HHDM(avail);
    viodev->common_cfg->queue_device = (uint64_t)PHYSICAL_HHDM(used);

    struct virtq *vq = kmalloc(sizeof(struct virtq));
    vq->viodev = viodev;
    vq->index = index;
    vq->num = num;
    vq->last_index = 0;
    vq->avail_index = 0;
    vq->notify = (void *)viodev->notify_base + (viodev->common_cfg->queue_notify_off * viodev->notify_base->notify_off_multiplier);
    vq->desc = desc;
    vq->avail = avail;
    vq->used = used;

    for (int i = 0; i < num - 1; i++)
        vq->desc[i].next = i + 1;
    vq->desc[num - 1].next = 0;

    viodev->common_cfg->queue_enable = 1;
    viodev->queues[index] = vq;
    return vq;
}

void virtio_add_buffer(struct virtq *vq, void *phys, uint32_t len) {
    uint16_t index = vq->last_index;
    vq->last_index = vq->desc[index].next;

    struct virtq_desc *desc = &vq->desc[index];
    desc->addr = (uint64_t)phys;
    desc->len = len;
    desc->flags = VIRTQ_DESC_F_WRITE;
    desc->next = 0;

    vq->avail->ring[vq->avail_index % vq->num] = index;
    vq->avail_index++;
    vq->avail->idx = vq->avail_index;
}

void virtio_initialize_device(pci_device *dev) {
    dprintf(LOG_DEBUG, "\033[93mvirtio:\033[0m initializing virtio device %04x:%04x\n", dev->vendor_id, dev->device_id);
    if (dev->device_id < 0x1040) {
        dprintf(LOG_DEBUG, "\033[93mvirtio:\033[0m transitional virtio devices not supported!\n");
        return;
    }

    pci_config_set_bits(dev->address, PCI_BUS_MASTER | PCI_IO_SPACE);

    struct virtio_device *viodev = kmalloc(sizeof(struct virtio_device));
    viodev->type = dev->device_id - 0x1040;

    uint8_t status = pci_config_read_word(dev->address, 0x06);
    assert(status & (1 << 4));

    list_t *caps = pci_find_all_caps(dev, 0x09);
    foreach(i, caps) {
        pci_capability *cap = i->value;

        uint32_t dword1 = pci_read(dev->address, cap->offset + 0);
        uint32_t dword2 = pci_read(dev->address, cap->offset + 4);
        uint32_t offset = pci_read(dev->address, cap->offset + 8);
        uint32_t length = pci_read(dev->address, cap->offset + 12);

        uint8_t type = (dword1 >> 24) & 0xFF;
        uint8_t bar = dword2 & 0xFF;
        void *bar_base = pci_map_bar(dev, bar, ALIGN_UP(offset + length, PAGE_SIZE) / PAGE_SIZE);
        assert(bar_base);
        void *region = (void *)((uintptr_t)bar_base + offset);

        switch (type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                viodev->common_cfg = region;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                viodev->notify_base = region;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                viodev->isr_base = region;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                viodev->device_cfg = region;
                break;
        }
    }

    assert(viodev->common_cfg);
    assert(viodev->device_cfg);

    viodev->common_cfg->device_status = 0;
    viodev->common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;

    viodev->num_queues = viodev->common_cfg->num_queues;
    viodev->queues = kmalloc(sizeof(struct virtq *) * viodev->num_queues);
    memset(viodev->queues, 0, sizeof(struct virtq *) * viodev->num_queues);

    list_insert(virtio_devices, viodev);
}

list_t *virtio_find_devices(enum virtio_device_type type) {
    list_t *devices = list_create();
    if (!virtio_devices)
        return devices;

    foreach(i, virtio_devices) {
        struct virtio_device *viodev = i->value;
        if (viodev->type == type)
            list_insert(devices, viodev);
    }

    return devices;
}

int init() {
    list_t *devices = pci_get_devices_from_vendor(VIRTIO_VENDOR);
    if (!devices->length) {
        dprintf(LOG_INFO, "\033[93mvirtio:\033[0m no devices found\n");
        list_free(devices);
        return -ENODEV;
    }

    virtio_devices = list_create();

    foreach(i, devices) {
        pci_device *dev = i->value;
        virtio_initialize_device(dev);
    }

    return 0;
}

int fini() {
    dprintf(LOG_INFO, "\033[93mvirtio:\033[0m Goodbye!\n");
    return 0;
}

struct Module metadata = {
    .name = "Virtio",
    .init = init,
    .fini = fini
};