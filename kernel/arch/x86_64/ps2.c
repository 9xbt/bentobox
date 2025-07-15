#include <bits/types/struct_timeval.h>
#include <linux/input.h>
#include <stdbool.h>
#include <ioctls.h>
#include <errno.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/video.h>
#include <kernel/acpi.h>
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
    [0x5D] = '\\', [0x6B] = 'D'+65535, [0x72] = 'B'+65535, [0x74] = 'C'+65535,
    [0x75] = 'A'+65535, [0x7C] = '*', [0x7B] = '-', [0x79] = '+',
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

bool kb_caps = false;
bool kb_ctrl = false;
bool kb_shift = false;

void irq1_handler(struct registers *r) {
    int c = 0;
    uint8_t key = inb(0x60);
    static uint8_t last_key = 0;
    if (last_key == 0xf0) {
        switch (key) {
            case 0x12:
            case 0x59:
                kb_shift = false;
                break;
            case 0x14:
                kb_ctrl = false;
                break;
            case 0xe0:
                break;
            default:
                if (kb_shift) {
                    c = kb_map_keys_shift[key];
                } else if (kb_caps) {
                    c = kb_map_keys_caps[key];
                } else {
                    c = kb_map_keys[key];
                }
                
                if (c < 65535) {
                    tty_enqueue(-c);
                }
                break;
        }
    } else {
        switch (key) {
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
            case 0xe0:
                break;
            default:
                if (kb_ctrl && kb_map_keys_caps[key] >= 'A' && kb_map_keys_caps[key] <= 'Z') {
                    c = kb_map_keys_caps[key] - 'A' + 1;
                } else if (kb_shift) {
                    c = kb_map_keys_shift[key];
                } else if (kb_caps) {
                    c = kb_map_keys_caps[key];
                } else {
                    c = kb_map_keys[key];
                }

                if (c > 65535) {
                    tty_enqueue('\033');
                    tty_enqueue('[');
                    tty_enqueue(c-65535);
                    tty_enqueue('\0');
                } else {
                    tty_enqueue(c);
                }
                break;
        }
    }

    last_key = key;
    lapic_eoi();
}

void irq12_handler(struct registers *r) {
    uint8_t status = inb(PS2_STATUS);
    if (!(status & (1 << 5))) {
        lapic_eoi();
        return;
    }
    inb(PS2_DATA);
    lapic_eoi();
}

static int ascii_to_linux_keycode(char c) {
    switch (c) {
        case 'a': case 'A': return KEY_A;
        case 'b': case 'B': return KEY_B;
        case 'c': case 'C': return KEY_C;
        case 'd': case 'D': return KEY_D;
        case 'e': case 'E': return KEY_E;
        case 'f': case 'F': return KEY_F;
        case 'g': case 'G': return KEY_G;
        case 'h': case 'H': return KEY_H;
        case 'i': case 'I': return KEY_I;
        case 'j': case 'J': return KEY_J;
        case 'k': case 'K': return KEY_K;
        case 'l': case 'L': return KEY_L;
        case 'm': case 'M': return KEY_M;
        case 'n': case 'N': return KEY_N;
        case 'o': case 'O': return KEY_O;
        case 'p': case 'P': return KEY_P;
        case 'q': case 'Q': return KEY_Q;
        case 'r': case 'R': return KEY_R;
        case 's': case 'S': return KEY_S;
        case 't': case 'T': return KEY_T;
        case 'u': case 'U': return KEY_U;
        case 'v': case 'V': return KEY_V;
        case 'w': case 'W': return KEY_W;
        case 'x': case 'X': return KEY_X;
        case 'y': case 'Y': return KEY_Y;
        case 'z': case 'Z': return KEY_Z;
        
        case '0': return KEY_0;
        case '1': return KEY_1;
        case '2': return KEY_2;
        case '3': return KEY_3;
        case '4': return KEY_4;
        case '5': return KEY_5;
        case '6': return KEY_6;
        case '7': return KEY_7;
        case '8': return KEY_8;
        case '9': return KEY_9;
        
        case ' ':  return KEY_SPACE;
        case '\t': return KEY_TAB;
        case '\n': return KEY_ENTER;
        case '\r': return KEY_ENTER;
        case '\b': return KEY_BACKSPACE;
        case 127:  return KEY_DELETE;
        case 27:   return KEY_ESC;
        
        case '-':  return KEY_MINUS;
        case '=':  return KEY_EQUAL;
        case '[':  return KEY_LEFTBRACE;
        case ']':  return KEY_RIGHTBRACE;
        case '\\': return KEY_BACKSLASH;
        case ';':  return KEY_SEMICOLON;
        case '\'': return KEY_APOSTROPHE;
        case '`':  return KEY_GRAVE;
        case ',':  return KEY_COMMA;
        case '.':  return KEY_DOT;
        case '/':  return KEY_SLASH;
        
        case '_': return KEY_MINUS;
        case '+': return KEY_EQUAL;
        case '{': return KEY_LEFTBRACE;
        case '}': return KEY_RIGHTBRACE;
        case '|': return KEY_BACKSLASH;
        case ':': return KEY_SEMICOLON;
        case '"': return KEY_APOSTROPHE;
        case '~': return KEY_GRAVE;
        case '<': return KEY_COMMA;
        case '>': return KEY_DOT;
        case '?': return KEY_SLASH;
        case '!': return KEY_1;
        case '@': return KEY_2;
        case '#': return KEY_3;
        case '$': return KEY_4;
        case '%': return KEY_5;
        case '^': return KEY_6;
        case '&': return KEY_7;
        case '*': return KEY_8;
        case '(': return KEY_9;
        case ')': return KEY_0;
        
        default: return -1;
    }
}

long ps2_keyboard_read_event(struct vfs_node *node, void *buffer, long offset, size_t len) {
    int c = tty_dequeue(false);
    if (c == -EAGAIN) return 0;

    struct input_event iev;
    iev.type = EV_KEY;
    iev.code = ascii_to_linux_keycode(c > 0 ? c : -c);
    iev.value = c > 0;
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

long ps2_mouse_read_event(struct vfs_node *node, void *buffer, long offset, size_t len) {
    return 0;
}

void ps2_send_command(uint8_t command) {
    while (inb(PS2_STATUS) & (1 << 1));
    outb(PS2_COMMAND, command);
}

void ps2_write_data(uint8_t data) {
    while (inb(PS2_STATUS) & (1 << 1));
    outb(PS2_DATA, data);
}

uint8_t ps2_read_data(void) {
    while (!(inb(PS2_STATUS) & (1 << 0)));
    return inb(PS2_DATA);
}

void ps2_send_mouse_command(uint8_t command) {
    while (inb(PS2_STATUS) & (1 << 1));
    outb(PS2_COMMAND, 0xD4);
    while (inb(PS2_STATUS) & (1 << 1));
    outb(PS2_DATA, command);
}

void ps2_wait_ack(void) {
    uint8_t ack = ps2_read_data();
    (void)ack;
}

void ps2_initialize(void) {
    if (!(fadt->iapc_boot_arch & (1 << 1))) {
        dprintf("%s:%d: warning: no PS/2 controller found\n", __FILE__, __LINE__);
        return;
    }

    uint8_t config;

    ps2_send_command(0xAD);
    ps2_send_command(0xA7);

    // Flush output buffer
    while (inb(PS2_STATUS) & (1 << 0)) {
        inb(PS2_DATA);
    }

    ps2_send_command(0x20);
    config = ps2_read_data();
    config &= ~((1 << 0) | (1 << 4) | (1 << 6));
    ps2_send_command(0x60);
    ps2_write_data(config);

    ps2_send_command(0xAA);
    uint8_t self_test = ps2_read_data();
    if (self_test != 0x55) {
        dprintf("%s:%d: self test failed\n", __FILE__, __LINE__);
        return;
    }

    bool dual_channel = false;
    ps2_send_command(0xA8);
    ps2_send_command(0x20);
    config = ps2_read_data();
    if (!(config & (1 << 5))) {
        dual_channel = true;
        ps2_send_command(0xA7);
        config &= ~((1 << 1) | (1 << 5));
        ps2_send_command(0x60);
        ps2_write_data(config);
    }

    bool port1_works = false, port2_works = false;

    ps2_send_command(0xAB);
    uint8_t port1_test = ps2_read_data();
    if (port1_test == 0x00) port1_works = true;

    if (dual_channel) {
        ps2_send_command(0xA9);
        uint8_t port2_test = ps2_read_data();
        if (port2_test == 0x00) port2_works = true;
    }

    if (port1_works) {
        ps2_send_command(0xAE);
        config |= (1 << 0);
    }
    if (port2_works) {
        ps2_send_command(0xA8);
        config |= (1 << 1);
    }

    if (port1_works || port2_works) {
        ps2_send_command(0x60);
        ps2_write_data(config);
    }

    if (port1_works) {
        irq_register(1, irq1_handler);
        struct vfs_node *event0 = vfs_create_node("event0", VFS_CHARDEVICE);
        event0->read = ps2_keyboard_read_event;
        vfs_add_node(vfs_open(NULL, "/dev/input", true, true), event0);
        dprintf("%s:%d: initialized keyboard\n", __FILE__, __LINE__);
    }

    if (port2_works) {
        ps2_send_mouse_command(0xF4);
        ps2_wait_ack();
        
        while (inb(PS2_STATUS) & (1 << 0)) {
            inb(PS2_DATA);
        }
        
        irq_register(12, irq12_handler);
        struct vfs_node *mouse = vfs_create_node("event1", VFS_CHARDEVICE);
        mouse->read = ps2_mouse_read_event;
        vfs_add_node(vfs_open(NULL, "/dev/input", true, true), mouse);
        dprintf("%s:%d: initialized mouse\n", __FILE__, __LINE__);
    }
}