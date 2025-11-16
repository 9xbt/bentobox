#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/fifo.h>

#define VIRTIO_MMIO_BASE 0x0a000000
#define VIRTIO_MMIO_SIZE 0x200

#define VIRTIO_MMIO_MAGIC              0x000
#define VIRTIO_MMIO_VERSION            0x004
#define VIRTIO_MMIO_DEVICE_ID          0x008
#define VIRTIO_MMIO_VENDOR_ID          0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES    0x010
#define VIRTIO_MMIO_DRIVER_FEATURES    0x020
#define VIRTIO_MMIO_QUEUE_SEL          0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX      0x034
#define VIRTIO_MMIO_QUEUE_NUM          0x038
#define VIRTIO_MMIO_QUEUE_READY        0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY       0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS   0x060
#define VIRTIO_MMIO_INTERRUPT_ACK      0x064
#define VIRTIO_MMIO_STATUS             0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW     0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH    0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW   0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH  0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW   0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH  0x0a4

#define VIRTIO_INPUT_CFG_UNSET      0x00
#define VIRTIO_INPUT_CFG_ID_NAME    0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL  0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS  0x03
#define VIRTIO_INPUT_CFG_PROP_BITS  0x10
#define VIRTIO_INPUT_CFG_EV_BITS    0x11
#define VIRTIO_INPUT_CFG_ABS_INFO   0x12

#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2
#define VIRTQ_DESC_F_INDIRECT   4

#define QUEUE_SIZE 64

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[QUEUE_SIZE];
};

struct virtio_queue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
};

struct virtio_device {
    volatile uint32_t *base;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t last_used_idx;
    int refcount;
    bool caps;
    bool ctrl;
    bool shift;
    struct fifo *fifo;
};

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
} __attribute__((packed));

void virtio_install(void);