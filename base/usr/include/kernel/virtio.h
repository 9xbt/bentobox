#pragma once
#include <kernel/input.h>
#include <kernel/fifo.h>
#include <kernel/list.h>
#include <kernel/vfs.h>
#include <stdint.h>

#define VIRTIO_VENDOR    0x1af4

#define VIRTIO_STATUS_ACKNOWLEDGE        1
#define VIRTIO_STATUS_DRIVER             2
#define VIRTIO_STATUS_DRIVER_OK          4
#define VIRTIO_STATUS_FEATURES_OK        8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED             128

#define VIRTIO_PCI_CAP_COMMON_CFG        1 
#define VIRTIO_PCI_CAP_NOTIFY_CFG        2 
#define VIRTIO_PCI_CAP_ISR_CFG           3 
#define VIRTIO_PCI_CAP_DEVICE_CFG        4 
#define VIRTIO_PCI_CAP_PCI_CFG           5 
#define VIRTIO_PCI_CAP_SHARED_MEMORY_CFG 8 
#define VIRTIO_PCI_CAP_VENDOR_CFG        9

struct virtio_pci_cap { 
    uint8_t cap_vndr;    /* Generic PCI field: PCI_CAP_ID_VNDR */ 
    uint8_t cap_next;    /* Generic PCI field: next ptr. */ 
    uint8_t cap_len;     /* Generic PCI field: capability length */ 
    uint8_t cfg_type;    /* Identifies the structure. */ 
    uint8_t bar;         /* Where to find it. */ 
    uint8_t id;          /* Multiple capabilities of the same type */ 
    uint8_t padding[2];  /* Pad to full dword. */ 
    uint32_t offset;     /* Offset within bar. */ 
    uint32_t length;     /* Length of the structure, in bytes. */ 
};

struct virtio_pci_common_cfg { 
    /* About the whole device. */ 
    uint32_t device_feature_select;     /* read-write */ 
    uint32_t device_feature;            /* read-only for driver */ 
    uint32_t driver_feature_select;     /* read-write */ 
    uint32_t driver_feature;            /* read-write */ 
    uint16_t config_msix_vector;        /* read-write */ 
    uint16_t num_queues;                /* read-only for driver */ 
    uint8_t device_status;              /* read-write */ 
    uint8_t config_generation;          /* read-only for driver */ 

    /* About a specific virtqueue. */ 
    uint16_t queue_select;              /* read-write */ 
    uint16_t queue_size;                /* read-write */ 
    uint16_t queue_msix_vector;         /* read-write */ 
    uint16_t queue_enable;              /* read-write */ 
    uint16_t queue_notify_off;          /* read-only for driver */ 
    uint64_t queue_desc;                /* read-write */ 
    uint64_t queue_driver;              /* read-write */ 
    uint64_t queue_device;              /* read-write */ 
    uint16_t queue_notify_data;         /* read-only for driver */ 
    uint16_t queue_reset;               /* read-write */ 
};

struct virtio_pci_notify_cap {
    struct virtio_pci_cap cap;
    uint32_t notify_off_multiplier; /* Multiplier for queue_notify_off. */
};

/* This marks a buffer as continuing via the next field. */ 
#define VIRTQ_DESC_F_NEXT       1 
/* This marks a buffer as write-only (otherwise read-only). */ 
#define VIRTQ_DESC_F_WRITE      2 
/* This means the buffer contains a list of buffer descriptors. */ 
#define VIRTQ_DESC_F_INDIRECT   4 
 
/* The device uses this in used->flags to advise the driver: don’t kick me 
 * when you add a buffer.  It’s unreliable, so it’s simply an 
 * optimization. */ 
#define VIRTQ_USED_F_NO_NOTIFY  1 
/* The driver uses this in avail->flags to advise the device: don’t 
 * interrupt me when you consume a buffer.  It’s unreliable, so it’s 
 * simply an optimization.  */ 
#define VIRTQ_AVAIL_F_NO_INTERRUPT      1 
 
/* Support for indirect descriptors */ 
#define VIRTIO_F_INDIRECT_DESC    28 
 
/* Support for avail_event and used_event fields */ 
#define VIRTIO_F_EVENT_IDX        29 
 
/* Arbitrary descriptor layouts. */ 
#define VIRTIO_F_ANY_LAYOUT       27 
 
/* Virtqueue descriptors: 16 bytes. 
 * These can chain together via "next". */ 
struct virtq_desc { 
    /* Address (guest-physical). */ 
    uint64_t addr; 
    /* Length. */ 
    uint32_t len; 
    /* The flags as indicated above. */ 
    uint16_t flags; 
    /* We chain unused descriptors via this, too */ 
    uint16_t next; 
}; 
 
struct virtq_avail { 
    uint16_t flags; 
    uint16_t idx; 
    uint16_t ring[]; 
    /* Only if VIRTIO_F_EVENT_IDX: uint16_t used_event; */ 
}; 
 
/* uint32_t is used here for ids for padding reasons. */ 
struct virtq_used_elem { 
    /* Index of start of used descriptor chain. */ 
    uint32_t id; 
    /* Total length of the descriptor chain which was written to. */ 
    uint32_t len; 
}; 
 
struct virtq_used { 
    uint16_t flags; 
    uint16_t idx; 
    struct virtq_used_elem ring[]; 
    /* Only if VIRTIO_F_EVENT_IDX: uint16_t avail_event; */ 
}; 
 
struct virtq {
    struct virtio_device *viodev;
    uint16_t index; 
    uint16_t num; 
    uint16_t last_index;
    uint16_t avail_index;
    volatile uint16_t *notify;

    struct virtq_desc *desc; 
    struct virtq_avail *avail; 
    struct virtq_used *used; 
}; 
 
static inline int virtq_need_event(uint16_t event_idx, uint16_t new_idx, uint16_t old_idx) 
{ 
    return (uint16_t)(new_idx - event_idx - 1) < (uint16_t)(new_idx - old_idx); 
} 
 
/* Get location of event indices (only with VIRTIO_F_EVENT_IDX) */ 
static inline uint16_t *virtq_used_event(struct virtq *vq) 
{ 
    /* For backwards compat, used event index is at *end* of avail ring. */ 
    return &vq->avail->ring[vq->num]; 
} 
 
static inline uint16_t *virtq_avail_event(struct virtq *vq) 
{ 
    /* For backwards compat, avail event index is at *end* of used ring. */ 
    return (uint16_t *)&vq->used->ring[vq->num]; 
} 

enum virtio_input_config_select {
    VIRTIO_INPUT_CFG_UNSET = 0x00,
    VIRTIO_INPUT_CFG_ID_NAME = 0x01,
    VIRTIO_INPUT_CFG_ID_SERIAL = 0x02,
    VIRTIO_INPUT_CFG_ID_DEVIDS = 0x03,
    VIRTIO_INPUT_CFG_PROP_BITS = 0x10,
    VIRTIO_INPUT_CFG_EV_BITS = 0x11,
    VIRTIO_INPUT_CFG_ABS_INFO = 0x12,
};

struct virtio_input_absinfo {
    uint32_t min;
    uint32_t max;
    uint32_t fuzz;
    uint32_t flat;
    uint32_t res;
};

struct virtio_input_devids {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

struct virtio_input_config {
    uint8_t select;
    uint8_t subsel;
    uint8_t size;
    uint8_t reserved[5];
    union {
        char string[128];
        uint8_t bitmap[128];
        struct virtio_input_absinfo abs;
        struct virtio_input_devids ids;
    } u;
};

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

enum virtio_device_type {
    VIRTIO_NIC   = 1,
    VIRTIO_BLOCK = 2,
    VIRTIO_INPUT = 18
};

struct virtio_device {
    volatile struct virtio_pci_common_cfg *common_cfg;
    volatile struct virtio_pci_notify_cap *notify_base;
    volatile void *isr_base;
    volatile void *device_cfg;
    enum virtio_device_type type;

    struct virtq **queues;
    uint16_t num_queues;
};

struct virtio_input_device {
    struct virtio_device *viodev;
    struct input_device *input_dev;
    struct fifo *fifo;
    struct vfs_node *tty;
    bool caps;
    bool shift;
    bool ctrl;
};

struct virtq *virtio_setup_virtqueue(struct virtio_device *viodev, uint16_t index);
void virtio_add_buffer(struct virtq *vq, void *phys, uint32_t len);
list_t *virtio_find_devices(enum virtio_device_type type);