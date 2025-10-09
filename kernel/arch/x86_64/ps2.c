#include <stdbool.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
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

enum {
    PS2_DATA = 0x60,
    PS2_STATUS = 0x64,
    PS2_COMMAND = 0x64
};

static bool kb_caps = false, kb_ctrl = false, kb_shift = false;
static struct fifo *kb_fifo, *mouse_fifo;
static vfs_node_t *tty, *kb, *mouse;

void irq1_handler(struct registers *r) {
    (void)r;
    
    unsigned char c = 0;
    uint8_t key = inb(0x60);
    static uint8_t last_key = 0;
    if (last_key == 0xf0) {
        switch (key) {
            case 0xe0:
                break;
            case 0x12:
            case 0x59:
                kb_shift = false;
                break;
            case 0x14:
                kb_ctrl = false;
                break;
        }
        fifo_enqueue(kb_fifo, -key);
    } else if (last_key == 0xe0) {
        switch (key) {
            case 0x75: // up
                tty->tty_ops->enqueue(tty, '\033');
                tty->tty_ops->enqueue(tty, '[');
                tty->tty_ops->enqueue(tty, 'A');
                break;
            case 0x72: // down
                tty->tty_ops->enqueue(tty, '\033');
                tty->tty_ops->enqueue(tty, '[');
                tty->tty_ops->enqueue(tty, 'B');
                break;
            case 0x74: // right
                tty->tty_ops->enqueue(tty, '\033');
                tty->tty_ops->enqueue(tty, '[');
                tty->tty_ops->enqueue(tty, 'C');
                break;
            case 0x6b: // left
                tty->tty_ops->enqueue(tty, '\033');
                tty->tty_ops->enqueue(tty, '[');
                tty->tty_ops->enqueue(tty, 'D');
                break;
        }
        fifo_enqueue(kb_fifo, key);
    } else {
        switch (key) {
            case 0xe0:
                break;
            case 0x12:
            case 0x59:
                kb_shift = true;
                break;
            case 0x14:
                kb_ctrl = true;
                break;
            case 0x58:
                kb_caps = !kb_caps;
                break;
            default:
                if (kb_ctrl) {
                    c = kb_map_keys_caps[key] - '@';
                } else if (kb_shift) {
                    c = kb_map_keys_shift[key];
                } else if (kb_caps) {
                    c = kb_map_keys_caps[key];
                } else {
                    c = kb_map_keys[key];
                }

                tty->tty_ops->enqueue(tty, c);
                break;
        }
        fifo_enqueue(kb_fifo, key);
    }

    last_key = key;
    lapic_eoi();
}

void irq12_handler(struct registers *r) {
    (void)r;

    static struct {
        bool left;
        bool right;
        bool middle;
        bool xs;
        bool ys;
        short delta_x;
        short delta_y;
    } state, last_state = {0};
    static int pi = 0;

    if (!(inb(PS2_STATUS) & (1 << 5))) {
        dprintf(LOG_INFO, "\033[93mi8042:\033[0m not a mouse packet\n");
        lapic_eoi();
        return;
    }

    uint8_t data = inb(PS2_DATA);
    if (pi == 0 && !(data & (1 << 3))) {
        dprintf(LOG_INFO, "\033[93mi8042:\033[0m corrupted mouse packet\n");
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
        #define PUSH(x) fifo_enqueue(mouse_fifo, x);
        if (state.delta_x) {
            PUSH(EV_REL);
            PUSH(REL_X);
            PUSH(state.delta_x);
        }
        if (state.delta_y) {
            PUSH(EV_REL);
            PUSH(REL_Y);
            PUSH(state.delta_y);
        }
        if (state.left != last_state.left) {
            PUSH(EV_KEY);
            PUSH(BTN_LEFT);
            PUSH(state.left);
        }
        if (state.right != last_state.right) {
            PUSH(EV_KEY);
            PUSH(BTN_RIGHT);
            PUSH(state.right);
        }
        if (state.middle != last_state.middle) {
            PUSH(EV_KEY);
            PUSH(BTN_MIDDLE);
            PUSH(state.middle);
        }

        memcpy(&last_state, &state, sizeof state);
        memset(&state, 0, sizeof state);
        pi = 0;

        // vfs_unblock_polling(mouse);
    }

    lapic_eoi();
}

static int scancode_linux_keycode(uint8_t scancode) {
    switch (scancode) {
        case 0x1C: return KEY_A;
        case 0x32: return KEY_B;
        case 0x21: return KEY_C;
        case 0x23: return KEY_D;
        case 0x24: return KEY_E;
        case 0x2B: return KEY_F;
        case 0x34: return KEY_G;
        case 0x33: return KEY_H;
        case 0x43: return KEY_I;
        case 0x3B: return KEY_J;
        case 0x42: return KEY_K;
        case 0x4B: return KEY_L;
        case 0x3A: return KEY_M;
        case 0x31: return KEY_N;
        case 0x44: return KEY_O;
        case 0x4D: return KEY_P;
        case 0x15: return KEY_Q;
        case 0x2D: return KEY_R;
        case 0x1B: return KEY_S;
        case 0x2C: return KEY_T;
        case 0x3C: return KEY_U;
        case 0x2A: return KEY_V;
        case 0x1D: return KEY_W;
        case 0x22: return KEY_X;
        case 0x35: return KEY_Y;
        case 0x1A: return KEY_Z;

        case 0x16: return KEY_1;
        case 0x1E: return KEY_2;
        case 0x26: return KEY_3;
        case 0x25: return KEY_4;
        case 0x2E: return KEY_5;
        case 0x36: return KEY_6;
        case 0x3D: return KEY_7;
        case 0x3E: return KEY_8;
        case 0x46: return KEY_9;
        case 0x45: return KEY_0;

        case 0x29: return KEY_SPACE;
        case 0x0D: return KEY_TAB;
        case 0x5A: return KEY_ENTER;
        case 0x66: return KEY_BACKSPACE;
        case 0x76: return KEY_ESC;

        case 0x4E: return KEY_MINUS;
        case 0x55: return KEY_EQUAL;
        case 0x54: return KEY_LEFTBRACE;
        case 0x5B: return KEY_RIGHTBRACE;
        case 0x5D: return KEY_BACKSLASH;
        case 0x4C: return KEY_SEMICOLON;
        case 0x52: return KEY_APOSTROPHE;
        case 0x0E: return KEY_GRAVE;
        case 0x41: return KEY_COMMA;
        case 0x49: return KEY_DOT;
        case 0x4A: return KEY_SLASH;

        case 0x14: return KEY_LEFTCTRL;
        case 0x12: return KEY_LEFTSHIFT;
        case 0x59: return KEY_RIGHTSHIFT;

        case 0x75: return KEY_UP;
        case 0x72: return KEY_DOWN;
        case 0x6b: return KEY_LEFT;
        case 0x74: return KEY_RIGHT;

        default: return -1;
    }
}

long ps2_keyboard_read_event(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    (void)len;

    int c;
    if (!fifo_dequeue(kb_fifo, &c)) return -EAGAIN;

    struct input_event iev;
    iev.type = EV_KEY;
    iev.code = scancode_linux_keycode(c > 0 ? c : -c);
    iev.value = c > 0;
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

long ps2_mouse_read_event(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    (void)offset;
    (void)len;

    if (fifo_is_empty(mouse_fifo)) return -EAGAIN;

    int packet[3] = {0};
    fifo_dequeue(mouse_fifo, &packet[0]);
    fifo_dequeue(mouse_fifo, &packet[1]);
    fifo_dequeue(mouse_fifo, &packet[2]);

    struct input_event iev;
    iev.type = packet[0];
    iev.code = packet[1];
    iev.value = packet[2];
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

static void ps2_wait_write(void) {
    while (inb(PS2_STATUS) & 2);
}

static void ps2_wait_read(void) {
    while (!(inb(PS2_STATUS) & 1));
}

void ps2_send_command(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_COMMAND, cmd);
}

void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA, data);
}

uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

void ps2_send_mouse_command(uint8_t cmd) {
    ps2_send_command(0xD4);
    ps2_write_data(cmd);
}

static void ps2_flush_buffer(void) {
    while (inb(PS2_STATUS) & 1) inb(PS2_DATA);
}

static void ps2_config_write(uint8_t config) {
    ps2_send_command(0x60);
    ps2_write_data(config);
}

vfs_ops_t keyboard_ops = {
    .read = ps2_keyboard_read_event
};

vfs_ops_t mouse_ops = {
    .read = ps2_mouse_read_event
};

void ps2_hid_install(void) {
    ps2_send_command(0xAD);
    ps2_send_command(0xA7);
    ps2_flush_buffer();

    ps2_send_command(0x20);
    uint8_t config = ps2_read_data() & ~0x51;
    ps2_config_write(config);

    ps2_send_command(0xAA);
    if (ps2_read_data() != 0x55) {
        dprintf(LOG_INFO, "\033[93mi8042:\033[0m self test failed\n");
        return;
    }

    ps2_send_command(0xA8);
    ps2_send_command(0x20);
    bool dual_channel = !(ps2_read_data() & 0x20);
    if (dual_channel) {
        ps2_send_command(0xA7);
        config &= ~0x22;
        ps2_config_write(config);
    }

    ps2_send_command(0xAB);
    bool port1_works = (ps2_read_data() == 0x00);
    bool port2_works = dual_channel && (ps2_send_command(0xA9), ps2_read_data() == 0x00);

    if (port1_works) {
        ps2_send_command(0xAE);
        config |= 1;
    }
    if (port2_works) {
        ps2_send_command(0xA8);
        config |= 2;
    }
    
    if (port1_works || port2_works) ps2_config_write(config);

    if (port1_works) {
        tty = vfs_lookup(NULL, "/dev/tty1", true, VFS_NONE);
        kb = vfs_create_node("event0", VFS_CHARDEVICE);
        kb->ops = &keyboard_ops;
        vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), kb);

        kb_fifo = fifo_create(64, int);
        irq_register(1, irq1_handler);
        ioapic_redirect_irq(0, 33, 1, false);
    }

    if (port2_works) {
        ps2_send_mouse_command(0xF4);
        ps2_read_data();
        ps2_flush_buffer();
        ps2_send_mouse_command(0xF3);
        ps2_read_data();
        ps2_send_mouse_command(100);
        ps2_read_data();
        
        mouse_fifo = fifo_create(64, int);
        mouse = vfs_create_node("event1", VFS_CHARDEVICE);
        mouse->ops = &mouse_ops;
        vfs_add_node(vfs_lookup(NULL, "/dev", true, VFS_DIRECTORY), mouse);

        irq_register(12, irq12_handler);
        ioapic_redirect_irq(0, 44, 12, false);
    }

    dprintf(LOG_INFO, "\033[93mi8042:\033[0m initialized PS/2 controller\n");
}