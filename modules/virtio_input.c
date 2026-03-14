#include <kernel/assert.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/virtio.h>
#include <kernel/list.h>

void vioinput_initialize_device(struct virtio_device *viodev) {
    volatile struct virtio_pci_common_cfg *cfg = viodev->common_cfg;

    cfg->device_status |= VIRTIO_STATUS_DRIVER;

    cfg->device_feature_select = 0;
    (void)cfg->device_feature;
    cfg->device_feature_select = 1;
    (void)cfg->device_feature;

    cfg->driver_feature_select = 0;
    cfg->driver_feature = 0;
    cfg->driver_feature_select = 1;
    cfg->driver_feature = 0;

    cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;

    assert(cfg->device_status & VIRTIO_STATUS_FEATURES_OK);
}

int init() {
    list_t *devices = vio_find_devices(VIRTIO_INPUT);
    foreach(i, devices) {
        struct virtio_device *viodev = i->value;
        vioinput_initialize_device(viodev);
    }
    return 0;
}

int fini() {
    return 0;
}

struct Module metadata = {
    .name = "virtio_input",
    .init = init,
    .fini = fini
};