#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __x86_64__
#include <kernel/arch/x86_64/mmu.h>
#elif __aarch64__
#include <kernel/arch/aarch64/mmu.h>
#endif

#define PAGE_SIZE       0x1000
#define PAGE_SIZE_2M    0x200000
#define PAGE_SIZE_1G    0x40000000

#define HEAP_BASE       0xffffc00000000000UL

#define VIRTUAL_HHDM(ptr) ((void *)((uintptr_t)(ptr) + (uintptr_t)hhdm_offset))
#define PHYSICAL_HHDM(ptr) ((void *)((uintptr_t)(ptr) - (uintptr_t)hhdm_offset))

#define DIV_CEILING(x, y) (x + (y - 1)) / y
#define ALIGN_UP(x, y) (DIV_CEILING(x, y) * y)
#define ALIGN_DOWN(x, y) ((x / y) * y)

extern uintptr_t hhdm_offset;
extern uintptr_t *kernel_pd;

void  mmu_initialize(void);
void *mmu_alloc(void);
void  mmu_free(void *ptr);
void  mmu_map_2mb(uintptr_t *pm, void *virt, void *phys, uint64_t flags);
void  mmu_map(uintptr_t *pm, void *virt, void *phys, uint64_t flags);
void  mmu_unmap_2mb(uintptr_t *pm, void *virt);
void  mmu_unmap(uintptr_t *pm, void *virt);
void  mmu_switch_pm(uintptr_t *pm);
uintptr_t mmu_get_physical(uintptr_t *pm, void *virt);
uint64_t  mmu_get_flags(uintptr_t *pm, void *virt);
void *mmu_map_module(uintptr_t base, size_t len);
void *mmu_map_module_bss(size_t pages);
uintptr_t *mmu_create_pagemap(void);