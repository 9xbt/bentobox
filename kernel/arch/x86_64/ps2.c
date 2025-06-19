#include <stdbool.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/acpi.h>
#include <kernel/fifo.h>
#include <kernel/sched.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/signal.h>

bool kb_caps = false;
bool kb_ctrl = false;
bool kb_shift = false;
struct fifo kb_fifo;

void irq1_handler(struct registers *r) {
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
                if (key >= sizeof(kb_map_keys)) {
                    break;
                }
                if (kb_ctrl && key == 0x2E) {
                    for (uint32_t id = 0; id < madt_lapics; id++) {
                        send_signal(get_core(id)->current_proc, SIGINT, 0);
                    }
                    break;
                }
                if (kb_shift) {
                    fifo_enqueue(&kb_fifo, kb_map_keys_shift[key]);
                } else if (kb_caps) {
                    fifo_enqueue(&kb_fifo, kb_map_keys_caps[key]);
                } else {
                    fifo_enqueue(&kb_fifo, kb_map_keys[key]);
                }
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
        }
    }
    lapic_eoi();
}

int getchar(void) {
    int c = 0;
    while (!fifo_dequeue(&kb_fifo, &c)) {
        //sched_block(TASK_BLOCKING_IO);
        //this->doing_blocking_io = true;
        sched_yield();
    }
    return c;
}

long ps2_keyboard_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    char *str = buffer;
    size_t i = 0;
    struct termios *tio = &this->fd_table[0].tio;

    if ((tio->c_lflag & ICANON) == 0) {
        int c = getchar();
        str[i++] = c;

        if (tio->c_lflag & ECHO)
            fprintf(stdout, "%c", c);
        return i;
    }

    while (i < len) {
        int c = getchar();
        str[i] = c;

        switch (c) {
            case '\0':
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

void ps2_initialize(void) {
    struct vfs_node *keyboard = vfs_create_node("keyboard", VFS_CHARDEVICE);
    keyboard->read = ps2_keyboard_read;
    keyboard->isatty = true;
    vfs_add_device(keyboard);
}

void ps2_install(void) {
    fifo_init(&kb_fifo, 64);
    irq_register(1, irq1_handler);
    dprintf("%s:%d: registered IRQ handler for PS/2 keyboard\n", __FILE__, __LINE__);
}