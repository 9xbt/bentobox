#pragma once
#include <stddef.h>

void *kmalloc(size_t n);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t size);