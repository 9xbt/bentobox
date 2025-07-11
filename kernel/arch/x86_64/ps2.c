#include <bits/types/struct_timeval.h>
#include <linux/input.h>
#include <errno.h>
#include <stdbool.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/video.h>
#include <kernel/vfs.h>
#include <kernel/tty.h>
#include <kernel/acpi.h>
#include <kernel/fifo.h>
#include <ioctls.h>
#include <kernel/sched.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/signal.h>

bool kb_caps = false;
bool kb_ctrl = false;
bool kb_shift = false;

void irq1_handler(struct registers *r) {
    int c = 0;
    uint8_t key = inb(0x60);
    if (!(key & 0x80)) {
        switch (key) {
            case 0x2a:
            case 0x36:
                kb_shift = true;
                break;
            case 0x1d:
                kb_ctrl = true;
                break;
            case 0x3a:
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
    } else {
        switch (key) {
            case 0xaa:
            case 0xb6:
                kb_shift = false;
                break;
            case 0x9d:
                kb_ctrl = false;
                break;
            case 0xe0:
                break;
            default:
                if (kb_shift) {
                    c = kb_map_keys_shift[key & 0x7F];
                } else if (kb_caps) {
                    c = kb_map_keys_caps[key & 0x7F];
                } else {
                    c = kb_map_keys[key & 0x7F];
                }
                
                if (c < 65535) {
                    tty_enqueue(-c);
                }
                break;
        }
    }
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

void ps2_initialize(void) {
    struct vfs_node *event0 = vfs_create_node("event0", VFS_CHARDEVICE);
    event0->read = ps2_keyboard_read_event;
    vfs_add_node(vfs_open(NULL, "/dev/input", true, true), event0);
}

void ps2_install(void) {
    irq_register(1, irq1_handler);
    dprintf("%s:%d: initialized keyboard\n", __FILE__, __LINE__);
}