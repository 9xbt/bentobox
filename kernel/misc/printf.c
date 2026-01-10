#include <stdarg.h>
#include <stdint.h>
#include <kernel/arch/aarch64/pl011.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/ringbuffer.h>
#include <kernel/lfbvideo.h>
#include <kernel/spinlock.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/time.h>

int loglevel = LOG_INFO;
spinlock_t flanterm_lock = 0;

static struct ringbuffer early_rb;
static unsigned char early_rb_buffer[4096];

struct ringbuffer *kernel_rb = &early_rb;

void early_log_initialize(void) {
    early_rb.buffer = early_rb_buffer;
    early_rb.write_ptr = 0;
    early_rb.read_ptr = 0;
    early_rb.size = sizeof(early_rb_buffer);
    early_rb.lock = 0;
}

void putchar(char c) {
    framebuffer_draw_cursor(-1, -1);
    acquire(&flanterm_lock);
    flanterm_write(ft_ctx, &c, 1);
    release(&flanterm_lock);
}

void puts(char *s) {
    framebuffer_draw_cursor(-1, -1);
    acquire(&flanterm_lock);
	flanterm_write(ft_ctx, s, strlen(s));
    release(&flanterm_lock);
}

int hex_length(uint64_t val) {
    int len = 0;
    do {
        len++; val >>= 4;
    } while (val != 0);
    return len;
}

int base_length(uint64_t val, uint32_t base) {
    int len = 1;
    while (val >= base) {
        val /= base;
        len++;
    }
    return len;
}

void parse_num(char *s, int *ptr, int64_t val, uint32_t base, bool is_signed, int width, char pad) {
    if (is_signed && val < 0) {
        s[(*ptr)++] = '-';
        val = -val;
    }
    for (int i = base_length(val, base); i < width; i++) {
        s[(*ptr)++] = pad;
    }
    uint64_t n = (uint64_t)val / base;
    int r = (uint64_t)val % base;
    if (val >= (int64_t)base) {
        parse_num(s, ptr, n, base, false, 0, 0);
    }
    s[(*ptr)++] = (r + '0');
}

void parse_hex(char *s, int *ptr, uint64_t val, int i) {
    if (!i) {
        i = hex_length(val);
    }
    while (i-- > 0) {
        s[(*ptr)++] = "0123456789abcdef"[val >> (i * 4) & 0x0F];
    }
}

void parse_string(char *s, int *ptr, char *str, bool unicode) {
    if (!str) {
        memcpy(s + *ptr, "(null)", 6);
        *ptr += 6;
        return;
    }
    if (unicode) {
        short *wstr = (short *)str;
        int len = wcslen(wstr);
        
        for (int i = 0; i < len; i++) {
            s[*ptr + i] = wstr[i];
        }
        *ptr += len;
    } else {
        strcpy(s + *ptr, str);
        *ptr += strlen(str);
    }
}

int vsprintf(char *s, const char *fmt, va_list args) {
    int ptr = 0;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            int width = 0; char pad = 0;
            if (*fmt == '0') {
                pad = '0';
                width = *++fmt - '0';
                fmt++;
            } else if (*fmt >= '0' && *fmt <= '9') {
                pad = ' ';
                width = *fmt++ - '0';
            }

            bool is_long = (*fmt == 'l') ? (fmt++, true) : false;

            switch (*fmt) {
                case 'u':
                    parse_num(s, &ptr, is_long ? va_arg(args, long) : va_arg(args, int), 10, false, width, pad);
                    break;
                case 'd':
                    parse_num(s, &ptr, is_long ? va_arg(args, long) : va_arg(args, int), 10, true, width, pad);
                    break;
                case 'x':
                    parse_hex(s, &ptr, is_long ? va_arg(args, uint64_t) : va_arg(args, uint32_t), width);
                    break;
                case 'p':
                    parse_hex(s, &ptr, va_arg(args, uint64_t), 16);
                    break;
                case 's':
                    parse_string(s, &ptr, va_arg(args, char *), is_long);
                    break;
                case 'c':
                    s[ptr++] = (char)va_arg(args, int);
                    break;
                case '%':
                    s[ptr++] = '%';
                    break;
            }
        } else {
            s[ptr++] = *fmt;
        }
        fmt++;
    }

    return ptr;
}

int snprintf(char *str, size_t n, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};
    int ret = vsprintf(buf, fmt, args);
    strncpy(str, buf, n);
    va_end(args);

    return ret;
}

int vprintf(const char *fmt, va_list args) {
    char buf[1024] = {-1};
    int ret = vsprintf(buf, fmt, args);
    
    puts(buf);
    return ret;
}

int dprintf(int level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};

    size_t secs = 0, nanos = 0;
    uptime(&secs, &nanos);

    int ret = vsprintf(buf + snprintf(buf, sizeof buf, "\033[32m[%5lu.%06lu]\033[0m ", secs, nanos / 1000), fmt, args);
    ringbuffer_write(kernel_rb, (unsigned char *)buf, strlen(buf));

    #ifdef __x86_64__
    serial_puts(buf);
    #elif __aarch64__
    uart_puts(buf);
    #endif
    
    if (level <= loglevel) {
        puts(buf);
    }
    va_end(args);
    return ret;
}

int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};
    int ret = vsprintf(buf, fmt, args);
    puts(buf);
    va_end(args);

    return ret;
}