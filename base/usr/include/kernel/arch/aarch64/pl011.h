#pragma once
#include <stdint.h>
#include <stddef.h>
#include <kernel/arch/aarch64/regs.h>

uint32_t pl011_read(uint32_t offset);
void pl011_write(uint32_t offset, uint32_t value);
int  pl011_is_bus_empty(void);
void uart_putchar(char c);
void uart_write(const char *s, size_t len);
void uart_puts(const char *str);
void pl011_irq_handler(struct registers *r);
void pl011_install(void);
void pl011_initialize(void);