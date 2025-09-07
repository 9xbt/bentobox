#include <stdarg.h>
#include <stdint.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/vfs.h>
#include <kernel/fd.h>

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
                    parse_hex(s, &ptr, is_long ? va_arg(args, uint64_t) : va_arg(args, uint32_t), 0);
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

int vprintf(const char *fmt, va_list args) {
    char buf[1024] = {-1};
    int ret = vsprintf(buf, fmt, args);
    
    extern void serial_puts(char *);
    serial_puts(buf);
    return ret;
}

int sprintf(char *str, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};
    int ret = vsprintf(buf, fmt, args);
    strcpy(str, buf);
    va_end(args);

    return ret;
}

int fprintf(int stream, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};
    int ret = vsprintf(buf, fmt, args);
    va_end(args);

    vfs_write(this->fd_table[stream].node, buf, 0, strlen(buf));

    return ret;
}

char *fgets(char *str, int n, int stream) {
    vfs_read(this->fd_table[stream].node, str, 0, n);
    return str;
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