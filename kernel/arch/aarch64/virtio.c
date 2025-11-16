#include <stdint.h>
#include <kernel/arch/aarch64/vectors.h>
#include <kernel/arch/aarch64/virtio.h>
#include <kernel/arch/aarch64/regs.h>
#include <kernel/arch/aarch64/gic.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/assert.h>
#include <kernel/input.h>
#include <kernel/errno.h>
#include <kernel/acpi.h>
#include <kernel/fifo.h>
#include <kernel/smp.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

static const char keycode_map[128] = {
    [2]  = '1',   [3]  = '2',    [4]  = '3',
    [5]  = '4',   [6]  = '5',    [7]  = '6',
    [8]  = '7',   [9]  = '8',    [10] = '9',
    [11] = '0',   [12] = '-',    [13] = '=',
    [14] = '\b',  [15] = '\t',   [16] = 'q',
    [17] = 'w',   [18] = 'e',    [19] = 'r',
    [20] = 't',   [21] = 'y',    [22] = 'u',
    [23] = 'i',   [24] = 'o',    [25] = 'p',
    [26] = '[',   [27] = ']',    [28] = '\n',
    [30] = 'a',   [31] = 's',    [32] = 'd',
    [33] = 'f',   [34] = 'g',    [35] = 'h',
    [36] = 'j',   [37] = 'k',    [38] = 'l',
    [39] = ';',   [40] = '\'',   [41] = '`',
    [43] = '\\',  [44] = 'z',    [45] = 'x',
    [46] = 'c',   [47] = 'v',    [48] = 'b',
    [49] = 'n',   [50] = 'm',    [51] = ',',
    [52] = '.',   [53] = '/',    [57] = ' ',
};

static const char keycode_map_shift[128] = {
    [2]  = '!',   [3]  = '@',    [4]  = '#',
    [5]  = '$',   [6]  = '%',    [7]  = '^',
    [8]  = '&',   [9]  = '*',    [10] = '(',
    [11] = ')',   [12] = '_',    [13] = '+',
    [14] = '\b',  [15] = '\t',   [16] = 'Q',
    [17] = 'W',   [18] = 'E',    [19] = 'R',
    [20] = 'T',   [21] = 'Y',    [22] = 'U',
    [23] = 'I',   [24] = 'O',    [25] = 'P',
    [26] = '{',   [27] = '}',    [28] = '\n',
    [30] = 'A',   [31] = 'S',    [32] = 'D',
    [33] = 'F',   [34] = 'G',    [35] = 'H',
    [36] = 'J',   [37] = 'K',    [38] = 'L',
    [39] = ':',   [40] = '"',    [41] = '~',
    [43] = '|',   [44] = 'Z',    [45] = 'X',
    [46] = 'C',   [47] = 'V',    [48] = 'B',
    [49] = 'N',   [50] = 'M',    [51] = '<',
    [52] = '>',   [53] = '?',    [57] = ' ',
};

static const char keycode_map_caps[128] = {
    [2]  = '1',   [3]  = '2',    [4]  = '3',
    [5]  = '4',   [6]  = '5',    [7]  = '6',
    [8]  = '7',   [9]  = '8',    [10] = '9',
    [11] = '0',   [12] = '-',    [13] = '=',
    [14] = '\b',  [15] = '\t',   [16] = 'Q',
    [17] = 'W',   [18] = 'E',    [19] = 'R',
    [20] = 'T',   [21] = 'Y',    [22] = 'U',
    [23] = 'I',   [24] = 'O',    [25] = 'P',
    [26] = '[',   [27] = ']',    [28] = '\n',
    [30] = 'A',   [31] = 'S',    [32] = 'D',
    [33] = 'F',   [34] = 'G',    [35] = 'H',
    [36] = 'J',   [37] = 'K',    [38] = 'L',
    [39] = ';',   [40] = '\'',   [41] = '`',
    [43] = '\\',  [44] = 'Z',    [45] = 'X',
    [46] = 'C',   [47] = 'V',    [48] = 'B',
    [49] = 'N',   [50] = 'M',    [51] = ',',
    [52] = '.',   [53] = '/',    [57] = ' ',
};

static struct virtio_device virtio_devices[32];
static vfs_node_t *tty;

void virtio_irq_handler(struct registers *r) {
    (void)r;
    struct virtio_device *dev = &virtio_devices[this_cpu->current_irq - 48];
    
    uint32_t status = dev->base[VIRTIO_MMIO_INTERRUPT_STATUS / 4];
    if (!(status & 1))
        return;
    
    while (dev->last_used_idx != dev->used->idx) {
        struct virtq_used_elem *elem = &dev->used->ring[dev->last_used_idx % QUEUE_SIZE];
        struct virtio_input_event *ev = (struct virtio_input_event *)VIRTUAL_HHDM(dev->desc[elem->id].addr);
        
        // dprintf(LOG_DEBUG, "Input event: type=%d code=%d value=%d\n", ev->type, ev->code, ev->value);

        if (__atomic_load_n(&dev->refcount, __ATOMIC_SEQ_CST)) {
            struct input_event iev = {
                .type = ev->type,
                .code = ev->code,
                .value = ev->value
            };
            fifo_enqueue(dev->fifo, iev);
        }

        if (ev->type == EV_KEY) {
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
                    if (ev->value == 1) {
                        tty->tty_ops->enqueue(tty, '\033');
                        tty->tty_ops->enqueue(tty, '[');
                        tty->tty_ops->enqueue(tty, 'A');
                    }
                    break;
                case KEY_DOWN:
                    if (ev->value == 1) {
                        tty->tty_ops->enqueue(tty, '\033');
                        tty->tty_ops->enqueue(tty, '[');
                        tty->tty_ops->enqueue(tty, 'B');
                    }
                    break;
                case KEY_RIGHT:
                    if (ev->value == 1) {
                        tty->tty_ops->enqueue(tty, '\033');
                        tty->tty_ops->enqueue(tty, '[');
                        tty->tty_ops->enqueue(tty, 'C');
                    }
                    break;
                case KEY_LEFT:
                    if (ev->value == 1) {
                        tty->tty_ops->enqueue(tty, '\033');
                        tty->tty_ops->enqueue(tty, '[');
                        tty->tty_ops->enqueue(tty, 'D');
                    }
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
                        tty->tty_ops->enqueue(tty, c);
                    }
                    break;
            }
        }
        
        dev->avail->ring[dev->avail->idx % QUEUE_SIZE] = elem->id;
        dev->avail->idx++;
        dev->last_used_idx++;
    }
    
    dev->base[VIRTIO_MMIO_QUEUE_NOTIFY / 4] = 0;
    dev->base[VIRTIO_MMIO_INTERRUPT_ACK / 4] = status;
}

long virtio_read_event(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    struct virtio_device *dev = node->device;

    if (len < sizeof(struct input_event))
        return -EINVAL;

    struct input_event iev;
    if (fifo_dequeue(dev->fifo, &iev) < (long)sizeof(struct input_event))
        return -EAGAIN;
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

long virtio_poll(vfs_node_t *node, long events) {
    struct virtio_device *dev = node->device;
    if (events & POLLIN) {
        if (!fifo_is_empty(dev->fifo))
            return POLLIN;
    }
    return 0;
}

long virtio_open(vfs_node_t *node, int flags) {
    (void)flags;
    struct virtio_device *dev = node->device;
    __atomic_add_fetch(&dev->refcount, 1, __ATOMIC_SEQ_CST);
    return 0;
}

long virtio_close(vfs_node_t *node) {
    struct virtio_device *dev = node->device;
    __atomic_sub_fetch(&dev->refcount, 1, __ATOMIC_SEQ_CST);
    return 0;
}

vfs_ops_t virtio_ops = {
    .read  = virtio_read_event,
    .poll  = virtio_poll,
    .open  = virtio_open,
    .close = virtio_close
};

void virtio_input_init(volatile uint32_t *base, int bus) {
    base[VIRTIO_MMIO_STATUS / 4] = 0;
    base[VIRTIO_MMIO_STATUS / 4] = 1;
    base[VIRTIO_MMIO_STATUS / 4] |= 2;
    base[VIRTIO_MMIO_DRIVER_FEATURES / 4] = 0;
    
    base[VIRTIO_MMIO_STATUS / 4] |= 8;
    uint32_t status = base[VIRTIO_MMIO_STATUS / 4];
    
    if (!(status & 8)) {
        dprintf(LOG_ERR, "\033[93mvirtio:\033[0m device rejected features!\n");
        return;
    }
    
    base[VIRTIO_MMIO_QUEUE_SEL / 4] = 0;
    assert(base[VIRTIO_MMIO_QUEUE_NUM_MAX / 4]);
    
    base[VIRTIO_MMIO_QUEUE_NUM / 4] = QUEUE_SIZE;
    
    struct virtq_desc *desc = VIRTUAL_HHDM(mmu_alloc());
    struct virtq_avail *avail = VIRTUAL_HHDM(mmu_alloc());
    struct virtq_used *used = VIRTUAL_HHDM(mmu_alloc());
    
    memset(desc, 0, PAGE_SIZE);
    memset(avail, 0, PAGE_SIZE);
    memset(used, 0, PAGE_SIZE);
    
    base[VIRTIO_MMIO_QUEUE_DESC_LOW / 4] = LOW((uint64_t)PHYSICAL_HHDM(desc));
    base[VIRTIO_MMIO_QUEUE_DESC_HIGH / 4] = HIGH((uint64_t)PHYSICAL_HHDM(desc));
    base[VIRTIO_MMIO_QUEUE_DRIVER_LOW / 4] = LOW((uint64_t)PHYSICAL_HHDM(avail));
    base[VIRTIO_MMIO_QUEUE_DRIVER_HIGH / 4] = HIGH((uint64_t)PHYSICAL_HHDM(avail));
    base[VIRTIO_MMIO_QUEUE_DEVICE_LOW / 4] = LOW((uint64_t)PHYSICAL_HHDM(used));
    base[VIRTIO_MMIO_QUEUE_DEVICE_HIGH / 4] = HIGH((uint64_t)PHYSICAL_HHDM(used));
    
    base[VIRTIO_MMIO_QUEUE_READY / 4] = 1;
    for (int i = 0; i < QUEUE_SIZE; i++) {
        struct virtio_input_event *ev = VIRTUAL_HHDM(mmu_alloc());
        memset(ev, 0, sizeof(struct virtio_input_event));
        
        desc[i].addr = (uint64_t)PHYSICAL_HHDM(ev);
        desc[i].len = sizeof(struct virtio_input_event);
        desc[i].flags = VIRTQ_DESC_F_WRITE;
        desc[i].next = 0;
        
        avail->ring[i] = i;
    }
    avail->idx = QUEUE_SIZE;

    base[VIRTIO_MMIO_QUEUE_NOTIFY / 4] = 0;
    base[VIRTIO_MMIO_STATUS / 4] |= 4;

    struct virtio_device *dev = &virtio_devices[bus];
    dev->base = base;
    dev->desc = desc;
    dev->avail = avail;
    dev->used = used;
    dev->last_used_idx = 0;
    dev->refcount = 0;
    dev->caps = 0;
    dev->ctrl = 0;
    dev->shift = 0;
    dev->fifo = fifo_create(256, struct input_event);

    vfs_node_t *node = devfs_create_event();
    node->device = dev;
    node->ops = &virtio_ops;

    int irq = 48 + bus;
    irq_register(irq, virtio_irq_handler);

    struct madt_gicd *gicd = madt_gicd_list[0];
    gicd_write(gicd, GICD_ISENABLER0 + irq / 8, 1 << (irq % 32));
}

void virtio_install(void) {
    tty = vfs_lookup(NULL, "/dev/tty1", true, VFS_NONE);

    int devices = 0;
    for (int i = 31; i >= 0; i--) {
        volatile uint32_t *base = VIRTUAL_HHDM(VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE));
        mmu_map(kernel_pd, (void *)base, PHYSICAL_HHDM(base), PTE_VALID | PTE_AF | PTE_RW | PTE_PXN);
        
        if (base[0] == 0x74726976 && base[2] == 18) {
            virtio_input_init(base, i);
            devices++;
        }
    }

    dprintf(LOG_INFO, "\033[93mvirtio:\033[0m found %d input device(s)\n", devices);
}