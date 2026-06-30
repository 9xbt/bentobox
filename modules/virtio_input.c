#include <kernel/assert.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/virtio.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/errno.h>
#include <kernel/input.h>
#include <kernel/list.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

long virtio_input_read_event(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    struct virtio_input_device *dev = node->device;
    if (len < sizeof(struct input_event))
        return -EINVAL;

    struct input_event iev;
    if (fifo_dequeue(dev->fifo, &iev) < (long)sizeof(struct input_event))
        return -EAGAIN;
    
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

static long virtio_input_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    (void)buffer;
    (void)offset;
    return len;
}

long virtio_input_poll(vfs_node_t *node, long events) {
    struct virtio_input_device *dev = node->device;
    if (!dev)
        return -EINVAL;

    long revents = 0;
    if (events & POLLIN && !fifo_is_empty(dev->fifo))
        revents |= POLLIN;
    if (events & POLLOUT)
        revents |= POLLOUT;
    return revents;
}

long virtio_input_ioctl(vfs_node_t *node, int op, void *arg) {
    struct virtio_input_device *dev = node->device;
    if (!dev)
        return -EINVAL;

    return input_generic_ioctl(dev->input_dev, op, arg);
}

vfs_ops_t virtio_input_ops = {
    .read  = virtio_input_read_event,
    .write = virtio_input_write,
    .poll  = virtio_input_poll,
    .ioctl = virtio_input_ioctl
};

void virtio_input_worker(vfs_node_t *node) {
    struct virtio_input_device *device = node->device;
    struct virtq *eventq = device->viodev->queues[0];
    for (;;) {
        while (eventq->last_index == eventq->used->idx)
            sched_yield();

        struct virtq_used_elem *elem = &eventq->used->ring[eventq->last_index % eventq->num];
        uint16_t desc_idx = elem->id;

        volatile struct virtio_input_event *event = VIRTUAL_HHDM(eventq->desc[desc_idx].addr);

        if (node->waiters->length > 0) {
            struct input_event iev = {
                .type  = event->type,
                .code  = event->code,
                .value = event->value
            };
            fifo_enqueue(device->fifo, iev);
            vfs_wake_waiters(node);
        }

        // dprintf(LOG_DEBUG, "event: type=%d code=%d value=%d\n", event->type, event->code, event->value);

        eventq->avail->ring[eventq->avail_index % eventq->num] = desc_idx;
        eventq->avail_index++;
        eventq->last_index++;

        eventq->avail->idx = eventq->avail_index;
        *eventq->notify = eventq->index;
    }
}

void virtio_input_initialize_device(struct virtio_device *viodev) {
    volatile struct virtio_pci_common_cfg *cfg = viodev->common_cfg;

    cfg->device_status |= VIRTIO_STATUS_DRIVER;
    cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    assert(cfg->device_status & VIRTIO_STATUS_FEATURES_OK);

    struct virtq *eventq = virtio_setup_virtqueue(viodev, 0);
    dprintf(LOG_DEBUG, "\033[93mvirtio:\033[0m eventq 0x%p\n", eventq);

    void *event_buf = mmu_alloc();
    for (int i = 0; i < eventq->num; i++) {
        virtio_add_buffer(eventq, event_buf + i * sizeof(struct virtio_input_event), sizeof(struct virtio_input_event));
    }
    
    cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    assert(cfg->device_status & VIRTIO_STATUS_DRIVER_OK);
    dprintf(LOG_DEBUG, "\033[93mvirtio:\033[0m input driver ok %d queues\n", viodev->num_queues);

    struct virtio_input_device *device = kmalloc(sizeof(struct virtio_input_device));
    device->viodev = viodev;
    device->input_dev = input_create(INPUT_KEYBOARD, BUS_VIRTUAL, VIRTIO_VENDOR, 0, 0);
    device->fifo = fifo_create(256, struct input_event);

    vfs_node_t *node = devfs_create_numbered(DEVFS_EVENT);
    node->ops = &virtio_input_ops;
    node->device = device;

    struct process *proc = sched_new_process("virtio input worker", false);
    sched_new_thread(proc, virtio_input_worker, 0, NULL, NULL, NULL, 0, node);
    sched_add_process(proc);

    dprintf(LOG_INFO, "\033[93mvirtio:\033[0m initialized input device\n");
}

int init() {
    list_t *devices = virtio_find_devices(VIRTIO_INPUT);
    foreach(i, devices) {
        struct virtio_device *viodev = i->value;
        virtio_input_initialize_device(viodev);
    }
    return 0;
}

int fini() {
    return 0;
}

struct Module metadata = {
    .name = "Virtio input",
    .init = init,
    .fini = fini
};