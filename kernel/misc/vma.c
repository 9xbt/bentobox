#include <stdint.h>
#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/mmu.h>

struct vma *vma_create(uintptr_t base, size_t size) {
    struct vma *vma = kmalloc(sizeof(struct vma));

    vma->pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    vma->bitmap = kmalloc(vma->pages / 8);
    vma->base = base;
    vma->used_pages = 0;
    memset(vma->bitmap, 0, vma->pages / 8);

    dprintf(LOG_INFO, "\033[93mvma:\033[0m created %ld MB pool\n", vma->pages / 256);
    return vma;
}

static size_t vma_find_pages(struct vma *vma, size_t page_count) {
    size_t pages = 0;
    size_t first_page = 0;

    for (size_t i = 0; i < vma->pages; i++) {
        if (!bitmap_get(vma->bitmap, i)) {
            if (pages == 0) {
                first_page = i;
            }
            pages++;
            if (pages == page_count) {
                for (size_t j = 0; j < page_count; j++) {
                    bitmap_set(vma->bitmap, first_page + j);
                }

                vma->used_pages += page_count;
                return first_page;
            }
        } else {
            pages = 0;
        }
    }
    return (size_t)-1;
}

void *vmalloc(struct vma *vma, uintptr_t *pm, size_t page_count, uint64_t flags) {
    size_t pages = vma_find_pages(vma, page_count);
    if (pages == (size_t)-1)
        return NULL;
    void *ptr = (void *)(pages * PAGE_SIZE + vma->base);
    for (size_t i = 0; i < page_count * PAGE_SIZE; i += PAGE_SIZE) {
        void *phys = mmu_alloc();
        mmu_map(pm, ptr + i, phys, flags);
        memset(VIRTUAL_HHDM(phys), 0, PAGE_SIZE);
    }
    return ptr;
}

void vfree(struct vma *vma, uintptr_t *pm, void *ptr, size_t page_count) {
    size_t page = (uintptr_t)ptr / PAGE_SIZE - vma->base;
    for (size_t i = 0; i < page_count; i++) {
        void *vaddr = ptr + (i * PAGE_SIZE);
        mmu_free((void *)mmu_get_physical(pm, vaddr));
        mmu_unmap(pm, vaddr);
        bitmap_clear(vma->bitmap, page + i);
    }
    vma->used_pages -= page_count;
}