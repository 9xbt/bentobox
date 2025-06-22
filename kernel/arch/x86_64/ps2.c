#include <bits/types/struct_timeval.h>
#include <linux/input.h>
#include <errno.h>
#include <stdbool.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/lfbvideo.h>
#include <kernel/vfs.h>
#include <kernel/acpi.h>
#include <kernel/fifo.h>
#include <kernel/ioctl.h>
#include <kernel/sched.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/signal.h>

bool kb_caps = false;
bool kb_ctrl = false;
bool kb_shift = false;
struct fifo kb_fifo;

void irq1_handler(struct registers *r) {
    int c;
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
            default:
                if (kb_ctrl && key == 0x2E) {
                    for (uint32_t id = 0; id < madt_lapics; id++) {
                        send_signal(get_core(id)->current_proc, SIGINT, 0);
                    }
                    break;
                }

                if (kb_shift) {
                    c = kb_map_keys_shift[key];
                } else if (kb_caps) {
                    c = kb_map_keys_caps[key];
                } else {
                    c = kb_map_keys[key];
                }

                if (c > 65535) {
                    fifo_enqueue(&kb_fifo, '\033');
                    fifo_enqueue(&kb_fifo, '[');
                    fifo_enqueue(&kb_fifo, c-65535);
                } else {
                    fifo_enqueue(&kb_fifo, c);
                }

                //if (kb_shift) {
                //    fifo_enqueue(&kb_fifo, kb_map_keys_shift[key]);
                //} else if (kb_caps) {
                //    fifo_enqueue(&kb_fifo, kb_map_keys_caps[key]);
                //} else {
                //    fifo_enqueue(&kb_fifo, kb_map_keys[key]);
                //}
                //sched_unblock_all_io();
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
            default:
                if (kb_shift) {
                    c = kb_map_keys_shift[key & 0x7F];
                } else if (kb_caps) {
                    c = kb_map_keys_caps[key & 0x7F];
                } else {
                    c = kb_map_keys[key & 0x7F];
                }
                
                if (c < 65535) {
                    fifo_enqueue(&kb_fifo, -c);
                }
                //sched_unblock_all_io();
                break;
        }
    }
    lapic_eoi();
}

int getchar(bool block) {
    int c = 0;
    while (!fifo_dequeue(&kb_fifo, &c)) {
        //sched_block(TASK_BLOCKING_IO);
        //this->doing_blocking_io = true;
        if (!block) {
            return -EAGAIN;
        }
        sched_yield();
    }
    return c;
}

long ps2_keyboard_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *str = buffer;
    size_t i = 0;
    struct termios *tio = &this->fd_table[0].tio;

    if ((tio->c_lflag & ICANON) == 0) {
        int c;
    again:
        c = getchar(tio->c_cc[VMIN] != 0);
        if (c > 0) str[i++] = c;
        else goto again;

        if (tio->c_lflag & ECHO)
            fprintf(stdout, "%c", c);
        return i;
    }

    while (i < len) {
        int c = getchar(true);
        if (c > 0) str[i] = c;
        else continue;

        switch (c) {
            case '\0':
            case '\t':
                break;
            case '\n':
            case '\r':
                if (tio->c_lflag & ECHO)
                    fprintf(stdout, "\n");
                str[i++] = '\n';
                str[i] = '\0';
                return i;
            case '\b':
            case 127:
                if (i > 0) {
                    if (tio->c_lflag & ECHO)
                        fprintf(stdout, "\b \b");
                    str[i] = '\0';
                    i--;
                }
                break;
            default:
                if (tio->c_lflag & ECHO)
                    fprintf(stdout, "%c", c);
                i++;
                break;
        }
    }

    return i;
}

int ascii_to_keycode(char c) {
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
    int c;
    if (!fifo_dequeue(&kb_fifo, &c))
        return 0;

    struct input_event iev;
    iev.type = EV_KEY;
    iev.code = ascii_to_keycode(c > 0 ? c : -c);
    iev.value = c > 0;
    memcpy(buffer, &iev, sizeof iev);
    return sizeof iev;
}

long ps2_ioctl(int fd_num, int op, void *arg) {
    static int mode = K_XLATE;

    struct fd *fd = &this->fd_table[0];
    switch (op) {
        case TCGETS:
            memcpy(arg, &fd->tio, sizeof(struct termios));
            break;
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            memcpy(&fd->tio, arg, sizeof(struct termios));
            break;
        case TIOCGWINSZ:
            lfb_get_ws((struct winsize *)arg);
            return 0;
        case TIOCGNAME:
            strcpy(arg, "/dev/keyboard");
            break;
        case KDGKBTYPE:
            *(int *)arg = KB_101;
            return 0;
        case KDGKBMODE:
            *(int *)arg = mode;
            return 0;
        case KDSKBMODE:
            mode = (unsigned long)arg;
            return 0;
        default:
            dprintf("%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
    return 0;
}

void ps2_initialize(void) {
    struct vfs_node *keyboard = vfs_create_node("keyboard", VFS_CHARDEVICE);
    keyboard->read = ps2_keyboard_read;
    keyboard->isatty = true;
    keyboard->ioctl = ps2_ioctl;
    vfs_add_device(keyboard);

    struct vfs_node *event0 = vfs_create_node("event0", VFS_CHARDEVICE);
    event0->read = ps2_keyboard_read_event;
    vfs_add_node(vfs_open(NULL, "/dev/input", true, true), event0);
}

void ps2_install(void) {
    fifo_init(&kb_fifo, 64);
    irq_register(1, irq1_handler);
    dprintf("%s:%d: registered IRQ handler for PS/2 keyboard\n", __FILE__, __LINE__);
}