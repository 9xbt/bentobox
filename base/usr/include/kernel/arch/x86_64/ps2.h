#pragma once
#include <stdbool.h>
#include <kernel/fifo.h>

#define PS2_STATUS_OUTPUT_FULL      0x01
#define PS2_STATUS_INPUT_FULL       0x02

#define PS2_CMD_READ_CONFIG         0x20
#define PS2_CMD_WRITE_TO_PORT2      0xD4

#define PS2_CONFIG_PORT1_IRQ        0x01
#define PS2_CONFIG_PORT2_IRQ        0x02
#define PS2_CONFIG_PORT1_TRANSLATE  0x40

#define PS2_MOUSE_SET_SAMPLE_RATE   0xF3
#define PS2_MOUSE_ENABLE_REPORTING  0xF4

struct ps2_device {
    int refcount;
    bool caps;
    bool ctrl;
    bool shift;
    struct fifo *fifo;
};

void ps2_hid_install(void);