#pragma once
#include <stdarg.h>
#include <kernel/arch/x86_64/smp.h>

void putchar(char c);
void puts(char *s);

int vsprintf(char *s, const char *fmt, va_list args);
int vprintf(const char *fmt, va_list args);
int sprintf(char *str, const char *fmt, ...);
int dprintf(int level, const char *fmt, ...);
int fprintf(int stream, const char *fmt, ...);
int printf(const char *fmt, ...);
char *fgets(char *str, int n, int stream);