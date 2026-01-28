#include <stdbool.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/lfbvideo.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/input.h>
#include <kernel/sched.h>
#include <kernel/acpi.h>
#include <kernel/fifo.h>
#include <kernel/fifo.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>
#include <flanterm.h>

static const int kb_map_keys[256] = {
    [0x15] = 'q', [0x1D] = 'w', [0x24] = 'e', [0x2D] = 'r', [0x2C] = 't',
    [0x35] = 'y', [0x3C] = 'u', [0x43] = 'i', [0x44] = 'o', [0x4D] = 'p',
    [0x54] = '[', [0x5B] = ']', [0x5A] = '\n',
    
    [0x1C] = 'a', [0x1B] = 's', [0x23] = 'd', [0x2B] = 'f', [0x34] = 'g',
    [0x33] = 'h', [0x3B] = 'j', [0x42] = 'k', [0x4B] = 'l', [0x4C] = ';',
    [0x52] = '\'', [0x0E] = '`',
    
    [0x1A] = 'z', [0x22] = 'x', [0x21] = 'c', [0x2A] = 'v', [0x32] = 'b',
    [0x31] = 'n', [0x3A] = 'm', [0x41] = ',', [0x49] = '.', [0x4A] = '/',
    
    [0x16] = '1', [0x1E] = '2', [0x26] = '3', [0x25] = '4', [0x2E] = '5',
    [0x36] = '6', [0x3D] = '7', [0x3E] = '8', [0x46] = '9', [0x45] = '0',
    [0x4E] = '-', [0x55] = '=', [0x66] = '\b',
    
    [0x0D] = '\t', [0x29] = ' ', [0x76] = 27,
    [0x5D] = '\\', [0x7C] = '*', [0x7B] = '-', [0x79] = '+',
};

static const int kb_map_keys_shift[256] = {
    [0x15] = 'Q', [0x1D] = 'W', [0x24] = 'E', [0x2D] = 'R', [0x2C] = 'T',
    [0x35] = 'Y', [0x3C] = 'U', [0x43] = 'I', [0x44] = 'O', [0x4D] = 'P',
    [0x54] = '{', [0x5B] = '}', [0x5A] = '\n',
    
    [0x1C] = 'A', [0x1B] = 'S', [0x23] = 'D', [0x2B] = 'F', [0x34] = 'G',
    [0x33] = 'H', [0x3B] = 'J', [0x42] = 'K', [0x4B] = 'L', [0x4C] = ':',
    [0x52] = '"', [0x0E] = '~',
    
    [0x1A] = 'Z', [0x22] = 'X', [0x21] = 'C', [0x2A] = 'V', [0x32] = 'B',
    [0x31] = 'N', [0x3A] = 'M', [0x41] = '<', [0x49] = '>', [0x4A] = '?',
    
    [0x16] = '!', [0x1E] = '@', [0x26] = '#', [0x25] = '$', [0x2E] = '%',
    [0x36] = '^', [0x3D] = '&', [0x3E] = '*', [0x46] = '(', [0x45] = ')',
    [0x4E] = '_', [0x55] = '+', [0x66] = '\b',
    
    [0x0D] = '\t', [0x29] = ' ', [0x76] = 27,
    [0x5D] = '|', [0x7C] = '*', [0x7B] = '-', [0x79] = '+',
};

static const int kb_map_keys_caps[256] = {
    [0x15] = 'Q', [0x1D] = 'W', [0x24] = 'E', [0x2D] = 'R', [0x2C] = 'T',
    [0x35] = 'Y', [0x3C] = 'U', [0x43] = 'I', [0x44] = 'O', [0x4D] = 'P',
    [0x54] = '[', [0x5B] = ']', [0x5A] = '\n',
    
    [0x1C] = 'A', [0x1B] = 'S', [0x23] = 'D', [0x2B] = 'F', [0x34] = 'G',
    [0x33] = 'H', [0x3B] = 'J', [0x42] = 'K', [0x4B] = 'L', [0x4C] = ';',
    [0x52] = '\'', [0x0E] = '`',
    
    [0x1A] = 'Z', [0x22] = 'X', [0x21] = 'C', [0x2A] = 'V', [0x32] = 'B',
    [0x31] = 'N', [0x3A] = 'M', [0x41] = ',', [0x49] = '.', [0x4A] = '/',
    
    [0x16] = '1', [0x1E] = '2', [0x26] = '3', [0x25] = '4', [0x2E] = '5',
    [0x36] = '6', [0x3D] = '7', [0x3E] = '8', [0x46] = '9', [0x45] = '0',
    [0x4E] = '-', [0x55] = '=', [0x66] = '\b',
    
    [0x0D] = '\t', [0x29] = ' ', [0x76] = 27,
    [0x5D] = '\\', [0x7C] = '*', [0x7B] = '-', [0x79] = '+',
};

static const int16_t keycode_map[128] = {
    [0x0D] = KEY_TAB,       [0x0E] = KEY_GRAVE,
    [0x12] = KEY_LEFTSHIFT, [0x14] = KEY_LEFTCTRL,  [0x11] = KEY_LEFTALT,
    [0x15] = KEY_Q,         [0x16] = KEY_1,         [0x1A] = KEY_Z,
    [0x1B] = KEY_S,         [0x1C] = KEY_A,         [0x1D] = KEY_W,
    [0x1E] = KEY_2,         [0x21] = KEY_C,         [0x22] = KEY_X,
    [0x23] = KEY_D,         [0x24] = KEY_E,         [0x25] = KEY_4,
    [0x26] = KEY_3,         [0x29] = KEY_SPACE,     [0x2A] = KEY_V,
    [0x2B] = KEY_F,         [0x2C] = KEY_T,         [0x2D] = KEY_R,
    [0x2E] = KEY_5,         [0x31] = KEY_N,         [0x32] = KEY_B,
    [0x33] = KEY_H,         [0x34] = KEY_G,         [0x35] = KEY_Y,
    [0x36] = KEY_6,         [0x3A] = KEY_M,         [0x3B] = KEY_J,
    [0x3C] = KEY_U,         [0x3D] = KEY_7,         [0x3E] = KEY_8,
    [0x41] = KEY_COMMA,     [0x42] = KEY_K,         [0x43] = KEY_I,
    [0x44] = KEY_O,         [0x45] = KEY_0,         [0x46] = KEY_9,
    [0x49] = KEY_DOT,       [0x4A] = KEY_SLASH,     [0x4B] = KEY_L,
    [0x4C] = KEY_SEMICOLON, [0x4D] = KEY_P,         [0x4E] = KEY_MINUS,
    [0x52] = KEY_APOSTROPHE,[0x54] = KEY_LEFTBRACE,
    [0x55] = KEY_EQUAL,     [0x59] = KEY_RIGHTSHIFT,[0x5A] = KEY_ENTER,
    [0x5B] = KEY_RIGHTBRACE,[0x5D] = KEY_BACKSLASH,
    [0x66] = KEY_BACKSPACE, [0x6B] = KEY_LEFT,      [0x72] = KEY_DOWN,
    [0x74] = KEY_RIGHT,     [0x75] = KEY_UP,        [0x76] = KEY_ESC,
};

enum {
    PS2_DATA = 0x60,
    PS2_STATUS = 0x64,
    PS2_COMMAND = 0x64
};

static vfs_node_t *tty, *kb, *mouse;

static void ps2_keyboard_enqueue_key(uint8_t key, int value) {
    struct ps2_device *dev = kb->device;
    if (__atomic_load_n(&dev->refcount, __ATOMIC_SEQ_CST)) {
        struct input_event iev = {
            .type = EV_KEY,
            .code = key < sizeof(keycode_map) / sizeof(int16_t) ? keycode_map[key] : 0,
            .value = value
        };
        fifo_enqueue(dev->fifo, iev);
    }
}

void irq1_handler(struct registers *r) {
    (void)r;
    
    int c;
    uint8_t key = inb(0x60);
    static uint8_t last_key = 0;
    struct ps2_device *dev = kb->device;
    if (last_key == 0xf0) {
        switch (key) {
            case 0xe0:
                break;
            case 0x12:
            case 0x59:
                dev->shift = false;
                break;
            case 0x14:
                dev->ctrl = false;
                break;
        }
        ps2_keyboard_enqueue_key(key, 0);
        vfs_wake_waiters(kb);
    } else if (last_key == 0xe0) {
        switch (key) {
            case 0x75: // up
                tty_enqueue_string(tty, "\033[A");
                break;
            case 0x72: // down
                tty_enqueue_string(tty, "\033[B");
                break;
            case 0x74: // right
                tty_enqueue_string(tty, "\033[C");
                break;
            case 0x6b: // left
                tty_enqueue_string(tty, "\033[D");
                break;
        }
        ps2_keyboard_enqueue_key(key, 1);
        vfs_wake_waiters(kb);
    } else {
        switch (key) {
            case 0xe0:
            case 0xf0:
                break;
            case 0x12:
            case 0x59:
                dev->shift = true;
                break;
            case 0x14:
                dev->ctrl = true;
                break;
            case 0x58:
                dev->caps = !dev->caps;
                break;
            default:
                if (dev->ctrl) {
                    c = kb_map_keys_caps[key] - '@';
                } else if (dev->shift) {
                    c = kb_map_keys_shift[key];
                } else if (dev->caps) {
                    c = kb_map_keys_caps[key];
                } else {
                    c = kb_map_keys[key];
                }

                tty->tty_ops->enqueue(tty, c);
                break;
        }
        ps2_keyboard_enqueue_key(key, 1);
        vfs_wake_waiters(kb);
    }

    last_key = key;
    lapic_eoi();
}

static inline void ps2_send_sgr_event(int button, int col, int row, bool release) {
    char buf[32];
    snprintf(buf, sizeof buf, "\e[<%d;%d;%d%c", button, col, row, release ? 'm' : 'M');
    tty_enqueue_string(tty, buf);
}

void irq12_handler(struct registers *r) {
    (void)r;
    static int pi = 0, x = 0, y = 0, last_col = -1, last_row = -1;

    static struct {
        bool left;
        bool right;
        bool middle;
        bool xs;
        bool ys;
        short delta_x;
        short delta_y;
    } state = {0}, last_state = {0};

    if (!(inb(PS2_STATUS) & (1 << 5))) {
        dprintf(LOG_ERR, "\033[93mi8042:\033[0m not a mouse packet\n");
        lapic_eoi();
        return;
    }

    uint8_t data = inb(PS2_DATA);
    if (pi == 0 && !(data & (1 << 3))) {
        dprintf(LOG_ERR, "\033[93mi8042:\033[0m corrupt mouse packet\n");
        lapic_eoi();
        return;
    }

    switch (pi) {
        case 0:
            state.left = data & (1 << 0);
            state.right = data & (1 << 1);
            state.middle = data & (1 << 2);
            state.xs = data & (1 << 4);
            state.ys = data & (1 << 5);
            break;
        case 1:
            state.delta_x = state.xs ? (data | 0xFF00) : data;
            break;
        case 2:
            state.delta_y = state.ys ? (data | 0xFF00) : data;
            break;
    }

    if (++pi >= 3) {
        struct ps2_device *dev = mouse->device;

        #define EMIT_REL(f, c) \
            if (state.f) { \
                struct input_event iev = { .type = EV_REL, .code = c, .value = state.f }; \
                fifo_enqueue(dev->fifo, iev); \
            }

        #define EMIT_KEY(f, c) \
            if (state.f != last_state.f) { \
                struct input_event iev = { .type = EV_KEY, .code = c, .value = state.f }; \
                fifo_enqueue(dev->fifo, iev); \
            }

        #define EMIT_SYN() \
            struct input_event iev = { .type = EV_SYN, .code = SYN_REPORT, .value = 0 }; \
            fifo_enqueue(dev->fifo, iev);

        if (__atomic_load_n(&dev->refcount, __ATOMIC_SEQ_CST)) {
            EMIT_REL(delta_x, REL_X);
            EMIT_REL(delta_y, REL_Y);
            EMIT_KEY(left, BTN_LEFT);
            EMIT_KEY(right, BTN_RIGHT);
            EMIT_KEY(middle, BTN_MIDDLE);
            EMIT_SYN();
        }

        x += state.delta_x;
        y -= state.delta_y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > (int)framebuffer->width) x = (int)framebuffer->width - 1;
        if (y > (int)framebuffer->height) y = (int)framebuffer->height - 1;

        tty_t *t = tty->device;
        if (t->mouse_tracking && t->sgr_mode) {
            size_t font_width, font_height;
            framebuffer_get_font_size(&font_width, &font_height);
            int col = (x / font_width) + 1;
            int row = (y / font_height) + 1;

            if (col != last_col || row != last_row) {
                if ((state.delta_x || state.delta_y) && (state.left || state.right || state.middle))
                    ps2_send_sgr_event(state.left ? 32 : (state.middle ? 33 : 34), col, row, false);
                last_col = col, last_row = row;
            }

            if (state.left && !last_state.left)     ps2_send_sgr_event(0, col, row, false);
            if (state.right && !last_state.right)   ps2_send_sgr_event(2, col, row, false);
            if (state.middle && !last_state.middle) ps2_send_sgr_event(1, col, row, false);
            if (!state.left && last_state.left)     ps2_send_sgr_event(0, col, row, true);
            if (!state.right && last_state.right)   ps2_send_sgr_event(2, col, row, true);
            if (!state.middle && last_state.middle) ps2_send_sgr_event(1, col, row, true);

            if (!state.left && !state.right && !state.middle)
                framebuffer_draw_cursor(x, y);
            else
                framebuffer_draw_cursor(-1, -1);
        }

        memcpy(&last_state, &state, sizeof state);
        memset(&state, 0, sizeof state);
        pi = 0;

        vfs_wake_waiters_irqsafe(mouse);
    }

    lapic_eoi();
}

long ps2_read_event(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)offset;
    struct ps2_device *dev = node->device;

    if (len < sizeof(struct input_event))
        return -EINVAL;

    struct input_event iev;
    if (fifo_dequeue(dev->fifo, &iev) < (long)sizeof(struct input_event))
        return -EAGAIN;
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

long ps2_poll(vfs_node_t *node, long events) {
    struct ps2_device *dev = node->device;
    if (events & POLLIN) {
        if (!fifo_is_empty(dev->fifo))
            return POLLIN;
    }
    return 0;
}

long ps2_open(vfs_node_t *node, int flags) {
    (void)flags;
    struct ps2_device *dev = node->device;
    __atomic_add_fetch(&dev->refcount, 1, __ATOMIC_SEQ_CST);
    return 0;
}

long ps2_close(vfs_node_t *node) {
    struct ps2_device *dev = node->device;
    __atomic_sub_fetch(&dev->refcount, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static void ps2_send_command(uint8_t cmd) {
    while (inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL) {}
    outb(PS2_COMMAND, cmd);
}

static void ps2_write_data(uint8_t data) {
    while (inb(PS2_STATUS) & PS2_STATUS_INPUT_FULL) {}
    outb(PS2_DATA, data);
}

static uint8_t ps2_read_data(void) {
    while (!(inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL)) {}
    return inb(PS2_DATA);
}

static void ps2_send_mouse_command(uint8_t cmd) {
    ps2_send_command(PS2_CMD_WRITE_TO_PORT2);
    ps2_write_data(cmd);
}

static void ps2_flush_buffer(void) {
    while (inb(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) {
        inb(PS2_DATA);
    }
}
static uint8_t ps2_config_read(void) {
    ps2_send_command(PS2_CMD_READ_CONFIG);
    return ps2_read_data();
}

static void ps2_config_write(uint8_t config) {
    ps2_send_command(0x60);
    ps2_write_data(config);
}

struct ps2_device keyboard_device = {0};
struct ps2_device mouse_device = {0};

vfs_ops_t ps2_ops = {
    .open = ps2_open,
    .close = ps2_close,
    .read = ps2_read_event,
    .poll = ps2_poll
};

void ps2_hid_install(void) {
    ps2_flush_buffer();
    ps2_config_write((ps2_config_read() & ~PS2_CONFIG_PORT1_TRANSLATE) | PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ);
    
    tty = vfs_lookup(NULL, "/dev/tty1", true, VFS_NONE);
    kb = devfs_create_numbered(DEVFS_EVENT);
    kb->ops = &ps2_ops;
    kb->device = &keyboard_device;
    keyboard_device.fifo = fifo_create(256, struct input_event);
    irq_register(1, irq1_handler);
    ioapic_redirect_irq(0, 33, 1, false);
    
    ps2_flush_buffer();
    ps2_send_mouse_command(PS2_MOUSE_ENABLE_REPORTING);
    if (ps2_read_data() != 0xFA)
        return;
    ps2_flush_buffer();
    
    ps2_send_mouse_command(PS2_MOUSE_SET_SAMPLE_RATE);
    ps2_read_data();
    ps2_send_mouse_command(100);
    ps2_read_data();
    
    mouse = devfs_create_numbered(DEVFS_EVENT);
    mouse->ops = &ps2_ops;
    mouse->device = &mouse_device;
    mouse_device.fifo = fifo_create(256, struct input_event);;
    irq_register(12, irq12_handler);
    ioapic_redirect_irq(0, 44, 12, false);
    
    dprintf(LOG_INFO, "\033[93mi8042:\033[0m initialized PS/2 controller\n");
}