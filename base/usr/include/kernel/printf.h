#pragma once
#include <stdarg.h>
#include <stddef.h>

enum {
    LOG_EMERG,
    LOG_ALERT,
    LOG_CRIT,
    LOG_ERR,
    LOG_WARNING,
    LOG_NOTICE,
    LOG_INFO,
    LOG_DEBUG
};

void putchar(char c);
void puts(char *s);

int vsprintf(char *s, const char *fmt, va_list args);
int snprintf(char *str, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
int dprintf(int level, const char *fmt, ...);
int printf(const char *fmt, ...);