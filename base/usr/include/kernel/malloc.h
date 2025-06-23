#pragma once
#include <stddef.h>

void *kmalloc(size_t n);
void  kfree(void *ptr);
void  malloc_initialize(void);