#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COM1        0x3f8
#define COM2        0x2f8
#define COM3        0x3e8
#define COM4        0x2e8
#define DEBUGCON    0xe9

bool serial_initialize(uint16_t port, uint8_t divisor);
void serial_write(const char *s, size_t len);
void serial_puts(const char *str);
void serial_install(void);