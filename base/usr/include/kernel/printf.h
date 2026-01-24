#pragma once
#include <stdarg.h>
#include <stddef.h>

#define KERNEL_LOG_SIZE (64 * 1024)

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

extern struct ringbuffer *kernel_rb;

void early_log_initialize(void);
void early_log_extend(void);
void putchar(char c);
void puts(char *s);

int vsprintf(char *s, const char *fmt, va_list args);
int snprintf(char *str, size_t n, const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
int dprintf(int level, const char *fmt, ...);
int printf(const char *fmt, ...);