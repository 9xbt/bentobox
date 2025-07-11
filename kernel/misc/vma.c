#include <stdbool.h>
#include <kernel/mmu.h>
#include <kernel/mmu/vma.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define VMA_BASE 0x555555554000
#define VMA_VIRTUAL(ptr) ((void *)((uintptr_t)(ptr) + (uintptr_t)VMA_BASE))
#define VMA_PHYSICAL(ptr) ((void *)((uintptr_t)(ptr) - (uintptr_t)VMA_BASE))

struct vma_head *vma_create(void) {
    struct vma_head *h = (struct vma_head *)VIRTUAL(mmu_alloc(1));
    mmu_map(h, PHYSICAL(h), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    h->head = (struct vma_block *)VIRTUAL(mmu_alloc(1));
    mmu_map(h->head, PHYSICAL(h->head), PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    h->head->next = h->head;
    h->head->prev = h->head;
    h->head->size = 0;
    h->head->checksum = 0;
    h->head->phys = 0;
    h->head->virt = 0;
    return h;
}

void vma_destroy(struct vma_head *h) {
    struct vma_block *current = h->head->next;
    struct vma_block *next;

    while (current != h->head) {
        next = current->next;
        if (current->virt) mmu_unmap_pages(current->size, (void *)current->virt);
        if (current->phys) mmu_free((void *)current->phys, current->size);
        mmu_unmap(current);
        mmu_free(PHYSICAL(current), 1);
        current = next;
    }

    mmu_free(PHYSICAL(h->head), 1);
    mmu_unmap(h->head);
    mmu_free(PHYSICAL(h), 1);
    mmu_unmap(h);
}

void *vma_map(struct vma_head *h, uint64_t pages, uint64_t phys, uint64_t virt, uint64_t flags) {
    struct vma_block *block = (struct vma_block *)VIRTUAL(mmu_alloc(1));
    mmu_map(block, PHYSICAL(block), PTE_PRESENT | PTE_WRITABLE);
    block->next = h->head;
    block->prev = h->head->prev;
    h->head->prev->next = block;
    h->head->prev = block;
    block->size = pages;
    if (flags) {
        if (phys) {
            block->phys = phys;
        } else {
            block->phys = (uintptr_t)mmu_alloc(pages);
        }
        if (virt) {
            block->virt = virt;
        } else {
            block->virt = (uintptr_t)VMA_VIRTUAL(block->phys);
        }
        mmu_map_pages(pages, (void *)block->virt, (void *)block->phys, flags);
    } else {
        block->phys = 0;
        block->virt = 0;
    }
    
    block->checksum = block->phys + block->virt;
    block->flags = flags;

    return (void *)block->virt;
}

void vma_copy_mappings(struct vma_head *dest, struct vma_head *src) {
    struct vma_block *current = src->head->next;

    while (current != src->head) {
        void *virt = vma_map(dest, current->size, 0, current->virt, current->flags);
        memcpy(virt, VIRTUAL_IDENT(current->phys), current->size * PAGE_SIZE);
        current = current->next;
    }
}

void vma_unmap(struct vma_block *block) {
    if (block->phys + block->virt != block->checksum) {
        printf("%s:%d: bad checksum at address 0x%x\n", __FILE__, __LINE__, (uint64_t)block);
        return;
    }

    block->prev->next = block->next;
    block->next->prev = block->prev;

    mmu_unmap_pages(block->size, (void *)block->virt);
    mmu_free((void *)block->phys, block->size);
    mmu_unmap(block);
    mmu_free(PHYSICAL(block), 1);
}

bool vma_unmap_addr(struct vma_head *h, void *virt) {
    uintptr_t target = (uintptr_t)virt;
    struct vma_block *current = h->head->next;

    while (current != h->head) {
        uintptr_t block_start = current->virt;
        uintptr_t block_end = current->virt + (current->size * PAGE_SIZE);
        
        if (target >= block_start && target < block_end) {
            vma_unmap(current);
            return true;
        }
        current = current->next;
    }
    return false;
}