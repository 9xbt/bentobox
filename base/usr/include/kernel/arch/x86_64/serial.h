#pragma once
#include <stddef.h>

#define COM1        0x3f8
#define DEBUGCON    0xe9

void serial_install(void);
void serial_write(const char *s, size_t len);
void serial_puts(const char *str);
void serial_initialize(void);