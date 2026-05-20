#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <kernel/log.h>

void putchar(const char c);
void puts(const char *s);
void write(int level, const char *s, size_t len);

int vsprintf(char *s, const char *fmt, va_list args);
int snprintf(char *str, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
int dprintf(int level, const char *fmt, ...);
int printf(const char *fmt, ...);