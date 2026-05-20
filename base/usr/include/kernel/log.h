#pragma once
#include <stddef.h>

#define MAX_LOG_SINKS   32
#define MAX_LOG_SIZE    (64 * 1024)

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

extern int kloglevel;
extern struct ringbuffer *kernel_rb;

typedef void (*log_handler)(int level, const char *s, size_t len);

void early_log_initialize(void);
void log_initialize(void);
void log_register_sink(log_handler handler);
void dputs(int level, const char *s);