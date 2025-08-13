#pragma once
#include <stdint.h>

void vga_clear(void);
void vga_puts(const char *str);
void vga_putchar(const char c);
void vga_scroll(void);
void vga_enable_cursor(void);
void vga_disable_cursor(void);
void vga_update_cursor(void);