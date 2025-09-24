#include <stdint.h>
#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/list.h>
#include <kernel/mmu.h>

struct vma *vma_create(uintptr_t base, size_t size) {
    struct vma *vma = kmalloc(sizeof(struct vma));

    vma->pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    vma->bitmap = kmalloc(ALIGN_UP(vma->pages, 8) / 8);
    vma->base = base;
    vma->used_pages = 0;
    vma->regions = list_create();
    memset(vma->bitmap, 0, ALIGN_UP(vma->pages, 8) / 8);

    dprintf(LOG_DEBUG, "\033[93mvma:\033[0m created %ld MB pool\n", vma->pages / 256);
    return vma;
}

void vma_destroy(struct vma *vma, uintptr_t *pm) {
    foreach(i, vma->regions) {
        struct vma_region *region = i->value;
        for (size_t i = 0; i < region->pages; i++) {
            void *vaddr = (void *)region->va + (i * PAGE_SIZE);
            mmu_free((void *)mmu_get_physical(pm, vaddr));
            mmu_unmap(pm, vaddr);
        }
        kfree(region);
    }

    list_free(vma->regions);

    for (uint64_t page = 0; page < vma->pages; page++) {
        if (bitmap_get(vma->bitmap, page)) {
            void *vaddr = (void *)(vma->base + page * PAGE_SIZE);
            mmu_free((void *)mmu_get_physical(pm, vaddr));
            mmu_unmap(pm, vaddr);
        }
    }

    kfree(vma->bitmap);
    kfree(vma);
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

void *vmalloc(struct vma *vma, uintptr_t *pm, uintptr_t va, size_t page_count, uint64_t flags) {
    void *ptr;
    if (!va) {
        size_t pages = vma_find_pages(vma, page_count);
        if (pages == (size_t)-1)
            return NULL;
        ptr = (void *)(pages * PAGE_SIZE + vma->base);
    } else {
        struct vma_region *region = kmalloc(sizeof(struct vma_region));
        region->pages = page_count;
        region->va = va;
        region->flags = flags;
        list_insert(vma->regions, region);
        ptr = (void *)va;
    }
    for (size_t i = 0; i < page_count * PAGE_SIZE; i += PAGE_SIZE) {
        void *phys = mmu_alloc();
        mmu_map(pm, ptr + i, phys, flags);
        memset(VIRTUAL_HHDM(phys), 0, PAGE_SIZE);
    }
    return ptr;
}

void vfree(struct vma *vma, uintptr_t *pm, void *ptr, size_t page_count) {
    foreach(i, vma->regions) {
        struct vma_region *region = i->value;
        if (region->va == (uintptr_t)ptr) {
            for (size_t i = 0; i < region->pages; i++) {
                void *vaddr = (void *)region->va + (i * PAGE_SIZE);
                mmu_free((void *)mmu_get_physical(pm, vaddr));
                mmu_unmap(pm, vaddr);
            }
            list_remove(vma->regions, i);
            kfree(region);
            return;
        }
    }

    if ((uintptr_t)ptr >= vma->base && (uintptr_t)ptr < vma->base + vma->pages * PAGE_SIZE) {
        size_t page = ((uintptr_t)ptr - vma->base) / PAGE_SIZE;
        for (size_t i = 0; i < page_count; i++) {
            void *vaddr = ptr + (i * PAGE_SIZE);
            mmu_free((void *)mmu_get_physical(pm, vaddr));
            mmu_unmap(pm, vaddr);
            bitmap_clear(vma->bitmap, page + i);
        }
        vma->used_pages -= page_count;
        return;
    }

    dprintf(LOG_WARNING, "\033[93mvma:\033[0m couldn't free region at 0x%p\n", ptr);
}