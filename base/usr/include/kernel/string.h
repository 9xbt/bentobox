#pragma once
#include <stddef.h>
#include <stdbool.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char* s);
size_t wcslen(const short* s);
bool  strcmp(const char* a, const char* b);
int   strncmp(const char *x, const char *y, register size_t n);
char *strcpy(char* dest, const char* src);
char *strncpy(char* dest, const char* src, size_t n);
char *strchr(register const char *s, int c);
char *strstr(const char *a, const char *b);
char *strtok(char *str, const char *delim);
char *strdup(const char *s);
int   atoi(char *s);