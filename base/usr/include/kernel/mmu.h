#pragma once
#include <stdint.h>
#include <stddef.h>
#include <kernel/spinlock.h>
#include <kernel/list.h>

#ifdef __x86_64__
#include <kernel/arch/x86_64/mmu.h>
#elif __aarch64__
#include <kernel/arch/aarch64/mmu.h>
#endif

#define PAGE_SIZE       0x1000
#define PAGE_SIZE_2M    0x200000
#define PAGE_SIZE_1G    0x40000000

#define VMA_KERNEL_BASE 0xffffa00000000000UL
#define HEAP_BASE       0xffffc00000000000UL

#define VIRTUAL_HHDM(ptr) ((void *)((uintptr_t)(ptr) + (uintptr_t)hhdm_offset))
#define PHYSICAL_HHDM(ptr) ((void *)((uintptr_t)(ptr) - (uintptr_t)hhdm_offset))

#define DIV_CEILING(x, y) (x + (y - 1)) / y
#define ALIGN_UP(x, y) (DIV_CEILING(x, y) * y)
#define ALIGN_DOWN(x, y) (((x) / y) * y)

#define LOW(x)  ((uint32_t)(x))
#define HIGH(x) ((uint32_t)((x) >> 32))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

extern uintptr_t hhdm_offset;
extern uintptr_t  *kernel_pd;
extern struct vma *kernel_vma;

void  mmu_initialize(void);
void  mmu_print_memory(void);
void *mmu_alloc(void);
void  mmu_free(void *ptr);
void  mmu_map_2mb(uintptr_t *pm, void *virt, void *phys, uint64_t flags);
void  mmu_map(uintptr_t *pm, void *virt, void *phys, uint64_t flags);
void  mmu_unmap_2mb(uintptr_t *pm, void *virt);
void  mmu_unmap(uintptr_t *pm, void *virt);
void  mmu_switch_pm(uintptr_t *pm);
void  mmu_destroy_pagemap(uintptr_t *pm);
uintptr_t mmu_get_physical(uintptr_t *pm, void *virt);
uint64_t  mmu_get_flags(uintptr_t *pm, void *virt);
void *mmu_map_module(uintptr_t base, size_t len);
void *mmu_map_module_bss(size_t pages);
uintptr_t *mmu_create_pagemap(void);
uintptr_t *mmu_get_pm(void);

struct vma {
    uint8_t *bitmap;
    size_t pages;
    size_t used_pages;
    size_t last_page;
    uintptr_t base;
    list_t *regions;
    spinlock_t lock;
};

struct vma_region {
    size_t pages;
    uintptr_t va;
    uintptr_t pa;
    uint64_t flags;
};

struct vma *vma_create(uintptr_t base, size_t size);
struct vma *vma_clone(struct vma *src, uintptr_t *pm);
void  vma_destroy(struct vma *vma, uintptr_t *pm);
void *vmalloc(struct vma *vma, uintptr_t *pm, uintptr_t va, uintptr_t pa, size_t page_count, uint64_t flags);
void  vfree(struct vma *vma, uintptr_t *pm, void *ptr, size_t page_count);

#define MAP_ANON  0x1000
#define MAP_FIXED   0x10
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

long check_user_address(const void *addr);
long copy_from_user(void *restrict dest, const void *restrict src, size_t n);
long copy_to_user(void *restrict dest, const void *restrict src, size_t n);
long strnlen_user(const char *s, size_t maxlen);

#define COPY_USER_STRING(name, user_ptr, max_len) \
    long name##_len = strnlen_user(user_ptr, max_len); \
    if (name##_len < 0) \
        return name##_len; \
    char *name = kmalloc(name##_len + 1); \
    copy_from_user(name, user_ptr, name##_len + 1);
