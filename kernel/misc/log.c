#include <kernel/ringbuffer.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/args.h>
#include <kernel/log.h>

int kloglevel = LOG_INFO;

log_handler log_sinks[MAX_LOG_SINKS] = {0};

static struct ringbuffer __early_rb;
static unsigned char __early_rb_buffer[4096] = {0};

struct ringbuffer *kernel_rb = &__early_rb;

void early_log_initialize(void) {
    __early_rb.buffer = __early_rb_buffer;
    __early_rb.write_ptr = 0;
    __early_rb.read_ptr = 0;
    __early_rb.size = sizeof(__early_rb_buffer);
    __early_rb.lock = 0;
    if (args_contains("loglevel"))
        kloglevel = atoi(args_value("loglevel"));
}

void log_initialize(void) {
    struct ringbuffer *rb = ringbuffer_create(MAX_LOG_SIZE);
    memcpy(rb->buffer, __early_rb.buffer, __early_rb.size);
    rb->read_ptr  = __early_rb.read_ptr;
    rb->write_ptr = __early_rb.write_ptr;
    kernel_rb     = rb;
}

void log_register_sink(log_handler handler) {
    for (int i = 0; i < MAX_LOG_SINKS; i++) {
        if (!log_sinks[i]) {
            log_sinks[i] = handler;
            char *ptr = (char *)kernel_rb->buffer;
            while (*ptr) {
                int len = strchr((const char *)ptr, '\n') - ptr;
                log_sinks[i](ptr[0], ptr + 1, len);
                ptr += len + 1;
            }
            return;
        }
    }
}

void dputs(int level, const char *s) {
    int len = strlen(s);
    ringbuffer_write(kernel_rb, (unsigned char *)s, len);

    for (int i = 0; i < MAX_LOG_SINKS; i++) {
        if (log_sinks[i]) {
            log_sinks[i](level, s + 1, len);
        }
    }
}