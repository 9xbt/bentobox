#pragma once
#include <stddef.h>

void *kmalloc_irqless(size_t n);
void  kfree_irqless(void *ptr);

void *kmalloc(size_t n);
void *kcalloc(size_t n, size_t size);
void  kfree(void *ptr);
void *krealloc(void *ptr, size_t size);