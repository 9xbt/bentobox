#pragma once
#include <kernel/list.h>
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

enum virtio_device_type {
    VIRTIO_NIC   = 1,
    VIRTIO_BLOCK = 2,
    VIRTIO_INPUT = 18
};

struct virtio_device {
    volatile struct virtio_pci_common_cfg *common_cfg;
    volatile void *notify_base;
    volatile void *isr_base;
    volatile void *device_cfg;
    enum virtio_device_type type;
};

list_t *vio_find_devices(enum virtio_device_type type);