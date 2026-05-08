#include <kernel/arch/x86_64/serial.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/regs.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/module.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/errno.h>
#include <kernel/input.h>
#include <kernel/sched.h>
#include <kernel/w8001.h>
#include <kernel/fifo.h>
#include <kernel/vfs.h>

static vfs_node_t *event;
static fifo_t *coord_fifo, *event_fifo;
static struct thread *worker_thread;

void wacom_parse_pen_data(unsigned char *data) {
    struct w8001_coord coord;

	coord.rdy = data[0] & 0x20;
	coord.tsw = data[0] & 0x01;
	coord.f1 = data[0] & 0x02;
	coord.f2 = data[0] & 0x04;

	coord.x = (data[1] & 0x7F) << 9;
	coord.x |= (data[2] & 0x7F) << 2;
	coord.x |= (data[6] & 0x60) >> 5;

	coord.y = (data[3] & 0x7F) << 9;
	coord.y |= (data[4] & 0x7F) << 2;
	coord.y |= (data[6] & 0x18) >> 3;

	coord.pen_pressure = data[5] & 0x7F;
	coord.pen_pressure |= (data[6] & 0x07) << 7 ;

	coord.tilt_x = data[7] & 0x7F;
	coord.tilt_y = data[8] & 0x7F;

    // dprintf(LOG_INFO, "X: %u Y: %u Pressure: %u\n", coord.x, coord.y, coord.pen_pressure);

    fifo_enqueue(coord_fifo, coord);
    sched_wake(worker_thread);
}

void wacom_worker(void) {
    struct w8001_coord coord = {0};

    for (;;) {
        if (fifo_dequeue(coord_fifo, &coord) < 0) {
            sched_block(this, 0);
        }

        #define EMIT_ABS(f, c) \
            do { \
                struct input_event iev = { .type = EV_ABS, .code = c, .value = f }; \
                fifo_enqueue(event_fifo, iev); \
            } while (0)

        #define EMIT_SYN() \
            do { \
                struct input_event iev = { .type = EV_SYN, .code = SYN_REPORT, .value = 0 }; \
                fifo_enqueue(event_fifo, iev); \
            } while (0)

        EMIT_ABS(coord.x, ABS_X);
        EMIT_ABS(coord.y, ABS_Y);
        EMIT_ABS(coord.pen_pressure, ABS_PRESSURE);
        EMIT_SYN();

        vfs_wake_waiters(event);
    }
}

void wacom_irq_handler(struct registers *r) {
    (void)r;

    static int pi = 0;
    static unsigned char data[W8001_MAX_LENGTH];

    uint8_t iir = inb(W8001_PORT + 2);
    while ((iir & 0x06) == 0x04) {
        char byte = inb(W8001_PORT);
        data[pi] = byte;
        switch (pi++) {
            case 0:
                if ((byte & W8001_LEAD_MASK) != W8001_LEAD_BYTE) {
                    dprintf(LOG_ERR, "\033[93mw8001:\033[0m unsynchronized data: 0x%02x\n", byte);
                    pi = 0;
                }
                break;
            case W8001_PKTLEN_TPCPEN - 1:
                if ((data[0] & W8001_TOUCH_BYTE) == W8001_TOUCH_BYTE)
                    break;

                pi = 0;
                wacom_parse_pen_data(data);
                break;
            default:
                if (pi > W8001_PKTLEN_TPCPEN - 1)
                    pi = 0;
                break;
        }

        iir = inb(W8001_PORT + 2);
    }

    lapic_eoi();
}

long wacom_read_event(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    if (len < sizeof(struct input_event))
        return -EINVAL;
    
    struct input_event iev;
    if (fifo_dequeue(event_fifo, &iev) < (long)sizeof(struct input_event))
        return -EAGAIN;
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

long wacom_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    (void)node;
    (void)buffer;
    (void)offset;
    return len;
}

long wacom_poll(vfs_node_t *node, long events) {
    (void)node;
    long revents = 0;
    if (events & POLLIN && !fifo_is_empty(event_fifo)) {
        revents |= POLLIN;
    }
    return revents;
}

long wacom_ioctl(vfs_node_t *node, int op, void *arg) {
    (void)node;
    if (!node->device)
        return -EINVAL;
    return input_generic_ioctl(node->device, op, arg);
}

vfs_ops_t wacom_ops = {
    .read  = wacom_read_event,
    .write = wacom_write,
    .poll  = wacom_poll,
    .ioctl = wacom_ioctl
};

int init() {
    if (!serial_initialize(W8001_PORT, 0x06)) {
        dprintf(LOG_INFO, "\033[93mw8001:\033[0m no devices found\n");
        return -ENODEV;
    }

    irq_register(4, wacom_irq_handler);
    ioapic_redirect_irq(0, 36, 4, false);
    outb(W8001_PORT + 1, 0x01);

    event = devfs_create_numbered(DEVFS_EVENT);
    event->ops = &wacom_ops;
    event->device = input_create(INPUT_TABLET, BUS_RS232, 0x056a, 0x90, 0x100);
    coord_fifo = fifo_create(256, struct w8001_coord);
    event_fifo = fifo_create(256, struct input_event);

    struct process *proc = sched_new_process("w8001 worker", false);
    worker_thread = sched_new_thread(proc, wacom_worker, 0, NULL, NULL, NULL, 0, NULL);
    worker_thread->state = THREAD_PAUSED;
    sched_add_process(proc);

    return 0;
}

int fini() {
    return 0;
}

struct Module metadata = {
    .name = "Wacom W8001 penabled serial driver",
    .init = init,
    .fini = fini
};