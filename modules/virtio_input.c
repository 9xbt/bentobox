/*
 * @package x86_64
 * @package aarch64
 */

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
#include <kernel/tty.h>

static const char keycode_map[128] = {
    [2]  = '1',  [3]  = '2',  [4]  = '3',  [5]  = '4',  [6]  = '5',  [7]  = '6',
    [8]  = '7',  [9]  = '8',  [10] = '9',  [11] = '0',  [12] = '-',  [13] = '=',
    [14] = '\b', [15] = '\t', [16] = 'q',  [17] = 'w',  [18] = 'e',  [19] = 'r',
    [20] = 't',  [21] = 'y',  [22] = 'u',  [23] = 'i',  [24] = 'o',  [25] = 'p',
    [26] = '[',  [27] = ']',  [28] = '\n', [30] = 'a',  [31] = 's',  [32] = 'd',
    [33] = 'f',  [34] = 'g',  [35] = 'h',  [36] = 'j',  [37] = 'k',  [38] = 'l',
    [39] = ';',  [40] = '\'', [41] = '`',  [43] = '\\', [44] = 'z',  [45] = 'x',
    [46] = 'c',  [47] = 'v',  [48] = 'b',  [49] = 'n',  [50] = 'm',  [51] = ',',
    [52] = '.',  [53] = '/',  [57] = ' ',
};

static const char keycode_map_shift[128] = {
    [2]  = '!',  [3]  = '@',  [4]  = '#',  [5]  = '$',  [6]  = '%',  [7]  = '^',
    [8]  = '&',  [9]  = '*',  [10] = '(',  [11] = ')',  [12] = '_',  [13] = '+',
    [14] = '\b', [15] = '\t', [16] = 'Q',  [17] = 'W',  [18] = 'E',  [19] = 'R',
    [20] = 'T',  [21] = 'Y',  [22] = 'U',  [23] = 'I',  [24] = 'O',  [25] = 'P',
    [26] = '{',  [27] = '}',  [28] = '\n', [30] = 'A',  [31] = 'S',  [32] = 'D',
    [33] = 'F',  [34] = 'G',  [35] = 'H',  [36] = 'J',  [37] = 'K',  [38] = 'L',
    [39] = ':',  [40] = '"',  [41] = '~',  [43] = '|',  [44] = 'Z',  [45] = 'X',
    [46] = 'C',  [47] = 'V',  [48] = 'B',  [49] = 'N',  [50] = 'M',  [51] = '<',
    [52] = '>',  [53] = '?',  [57] = ' ',
};

static const char keycode_map_caps[128] = {
    [2]  = '1',  [3]  = '2',  [4]  = '3',  [5]  = '4',  [6]  = '5',  [7]  = '6',
    [8]  = '7',  [9]  = '8',  [10] = '9',  [11] = '0',  [12] = '-',  [13] = '=',
    [14] = '\b', [15] = '\t', [16] = 'Q',  [17] = 'W',  [18] = 'E',  [19] = 'R',
    [20] = 'T',  [21] = 'Y',  [22] = 'U',  [23] = 'I',  [24] = 'O',  [25] = 'P',
    [26] = '[',  [27] = ']',  [28] = '\n', [30] = 'A',  [31] = 'S',  [32] = 'D',
    [33] = 'F',  [34] = 'G',  [35] = 'H',  [36] = 'J',  [37] = 'K',  [38] = 'L',
    [39] = ';',  [40] = '\'', [41] = '`',  [43] = '\\', [44] = 'Z',  [45] = 'X',
    [46] = 'C',  [47] = 'V',  [48] = 'B',  [49] = 'N',  [50] = 'M',  [51] = ',',
    [52] = '.',  [53] = '/',  [57] = ' ',
};

void virtio_input_parse_key_event(struct virtio_input_device *dev, struct virtio_input_event *ev) {
    switch (ev->code) {
        case KEY_CAPSLOCK:
            if (ev->value == 1)
                dev->caps = !dev->caps;
            break;
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
            dev->shift = ev->value;
            break;
        case KEY_LEFTCTRL:
            dev->ctrl = ev->value;
            break;
        case KEY_UP:
            if (ev->value == 1)
                tty_enqueue_string(dev->tty, "\033[A");
            break;
        case KEY_DOWN:
            if (ev->value == 1)
                tty_enqueue_string(dev->tty, "\033[B");
            break;
        case KEY_RIGHT:
            if (ev->value == 1)
                tty_enqueue_string(dev->tty, "\033[C");
            break;
        case KEY_LEFT:
            if (ev->value == 1)
                tty_enqueue_string(dev->tty, "\033[D");
            break;
        default:
            if (ev->value == 1) {
                int c;
                if (dev->ctrl) {
                    c = keycode_map_caps[ev->code] - '@';
                } else if (dev->shift) {
                    c = keycode_map_shift[ev->code];
                } else if (dev->caps) {
                    c = keycode_map_caps[ev->code];
                } else {
                    c = keycode_map[ev->code];
                }
                tty_enqueue(dev->tty, c);
            }
            break;
    }
}

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

        struct virtio_input_event event = *(volatile struct virtio_input_event *)VIRTUAL_HHDM(eventq->desc[desc_idx].addr);

        if (node->waiters->length > 0) {
            struct input_event iev = {
                .type  = event.type,
                .code  = event.code,
                .value = event.value
            };
            fifo_enqueue(device->fifo, iev);
            vfs_wake_waiters(node);
        }

        virtio_input_parse_key_event(device, &event);

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
    device->tty = vfs_lookup(NULL, "/dev/tty1", true, VFS_NONE).node;
    assert(device->tty);
    device->caps = device->shift = device->ctrl = false;

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
    if (!devices)
        return -ENODEV;

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