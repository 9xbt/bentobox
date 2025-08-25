#pragma once
#include <stdint.h>

#define PAGE_SIZE       0x1000
#define PAGE_SIZE_2M    0x200000
#define PAGE_SIZE_1G    0x40000000

#define VIRTUAL_HHDM(ptr) ((void *)((uintptr_t)(ptr) + (uintptr_t)hhdm_offset))
#define PHYSICAL_HHDM(ptr) ((void *)((uintptr_t)(ptr) - (uintptr_t)hhdm_offset))

#define DIV_CEILING(x, y) (x + (y - 1)) / y
#define ALIGN_UP(x, y) (DIV_CEILING(x, y) * y)
#define ALIGN_DOWN(x, y) ((x / y) * y)

extern uintptr_t hhdm_offset;

void  mmu_initialize(void);
void *mmu_alloc_frame(void);
void  mmu_free(void *ptr);