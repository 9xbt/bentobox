#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <kernel/sched.h>

struct vma_head {
    struct vma_block *head;
};

struct vma_block {
    struct vma_block *next;
    struct vma_block *prev;
    size_t size;
    size_t checksum;
    uintptr_t phys;
    uintptr_t virt;
    uint64_t flags;
};

struct vma_head *vma_create(void);
void vma_destroy(struct vma_head *h);
void *vma_map(struct vma_head *h, uint64_t pages, uint64_t phys, uint64_t virt, uint64_t flags);
void vma_unmap(struct vma_block *block);
void vma_copy_mappings(struct vma_head *dest, struct vma_head *src);
bool vma_unmap_addr(struct vma_head *h, void *virt);