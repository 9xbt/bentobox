#include <stdint.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/io.h>
#include <kernel/arch/x86_64/vga.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/spinlock.h>

#define CURSOR_HEIGHT 2

uint8_t vga_x = 0;
uint8_t vga_y = 0;
uint8_t vga_color = 0x07;
volatile uint16_t *vga_buffer = (volatile uint16_t *)0xB8000;

int vga_ansi_index = 0;
char vga_ansi_code[8] = {0};

atomic_flag vga_lock = ATOMIC_FLAG_INIT;

uint8_t ansi_to_vga(int ansi) {
    static uint8_t table[] = {
        0x00, 0x04, 0x02, 0x06, 0x01, 0x05, 0x03, 0x07,
        0x08, 0x0C, 0x0A, 0x0E, 0x09, 0x0D, 0x0B, 0x0F
    };

    if (ansi >= 30 && ansi <= 37)
        return table[ansi - 30];
    else if (ansi >= 40 && ansi <= 47)
        return table[ansi - 40];
    else if (ansi >= 90 && ansi <= 97)
        return table[ansi - 82];
    else if (ansi >= 100 && ansi <= 107)
        return table[ansi - 92];
    else return 0;
}

void vga_clear(void) {
    for (int i = vga_y * 80 + vga_x; i < 80 * 25; i++)
        vga_buffer[i] = vga_color << 8;
}

void vga_puts(const char *str) {
    acquire(&vga_lock);
    while (*str) {
        vga_putchar(*str++);
    }
    release(&vga_lock);
}

void vga_putchar(const char c) {
    if (vga_ansi_index >= 16) {
        vga_ansi_index = 0;
        memset(vga_ansi_code, 0, sizeof(vga_ansi_code));
    }

    if (vga_ansi_code[0] == '\033') {
        switch (c) {
            case '[':
                vga_ansi_code[1] = '[';
                vga_ansi_index = 2;
                return;
            case 'J':
                vga_clear();
                vga_ansi_index = 0;
                memset(vga_ansi_code, 0, sizeof(vga_ansi_code));
                return;
            case 'H':
                vga_x = 0, vga_y = 0, vga_ansi_index = 0;
                memset(vga_ansi_code, 0, sizeof(vga_ansi_code));
                return;
            case 'm':
                vga_ansi_code[vga_ansi_index] = 0;
                vga_ansi_index = 0;
                int code = atoi(vga_ansi_code + 2);
                if (code == 1) {
                    code = atoi(vga_ansi_code + 4);
                    code += 60;
                }
                if (code == 0) {
                    vga_color = 0x07;
                } else if (code == 39) {
                    vga_color = (vga_color & 0xF0) | 0x07;
                } else if (code == 49) {
                    vga_color = (vga_color & 0x0F) | (0x70);
                } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
                    vga_color = (vga_color & 0xF0) | (ansi_to_vga(code) & 0x0F);
                } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
                    vga_color = (vga_color & 0x0F) | ((ansi_to_vga(code) & 0x0F) << 4);
                }
                memset(vga_ansi_code, 0, sizeof(vga_ansi_code));
                return;
            default:
                vga_ansi_code[vga_ansi_index++] = c;
                return;
        }
    }

    switch (c) {
        case '\0':
        case '\07':
            break;
        case '\033':
            vga_ansi_code[0] = '\033';
            break;
        case '\n':
            vga_x = 80;
            break;
        case '\b':
            if (vga_x == 0) {
                vga_x = 79;
                vga_y--;
            } else vga_x--;
            vga_buffer[vga_y * 80 + vga_x] = vga_color << 8;
            break;
        case '\r':
            vga_x = 0;
            break;
        case '\t':
            for (int i = 0; i < 4; i++) {
                vga_x++;
                if (vga_x >= 80) { 
                    vga_x = 0;
                    vga_y++;
                }
            }
            break;
        default:
            vga_buffer[vga_y * 80 + vga_x] = (vga_color << 8) | c;
            vga_x++;
            break;
    }

    if (vga_x >= 80) { 
        vga_x = 0;
        vga_y++;
    }

    if (vga_y >= 25)
        vga_scroll();

    vga_update_cursor();
}

void vga_scroll(void) {
    for (int i = 0; i < 80 * 24; i++)
        vga_buffer[i] = vga_buffer[i + 80];
    for (int i = 0; i < 80; i++)
        vga_buffer[80 * 24 + i] = vga_color << 8;
    vga_y--;
}

void vga_enable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | (16 - CURSOR_HEIGHT));
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
}

void vga_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

void vga_update_cursor(void) {
	uint16_t pos = vga_y * 80 + vga_x;
 
	outb(0x3D4, 0x0F);
	outb(0x3D5, pos & 0xFF);
	outb(0x3D4, 0x0E);
	outb(0x3D5, (pos >> 8) & 0xFF);
}