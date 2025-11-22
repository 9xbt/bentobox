#include <stdint.h>
#include <kernel/assert.h>
#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/list.h>
#include <kernel/mmu.h>

struct vma *vma_create(uintptr_t base, size_t size) {
    struct vma *vma = kmalloc(sizeof(struct vma));

    vma->pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
    vma->bitmap = kmalloc(ALIGN_UP(vma->pages, 8) / 8);
    vma->base = base;
    vma->used_pages = 0;
    vma->last_page = 0;
    vma->regions = list_create();
    vma->lock = 0;
    memset(vma->bitmap, 0, ALIGN_UP(vma->pages, 8) / 8);

    return vma;
}

void vma_destroy(struct vma *vma, uintptr_t *pm) {
    acquire(&vma->lock);

    foreach(i, vma->regions) {
        struct vma_region *region = i->value;
        for (size_t j = 0; j < region->pages; j++) {
            void *vaddr = (void *)region->va + (j * PAGE_SIZE);
            if (!region->pa) {
                void *pa = (void *)mmu_get_physical(pm, vaddr);
                assert(pa);
                if (--(*mmu_get_refcount(pa)) == 0)
                    mmu_free(pa);
            }
            mmu_unmap(pm, vaddr);
        }

        if (region->va >= vma->base && region->va < vma->base + vma->pages * PAGE_SIZE) {
            size_t page = (region->va - vma->base) / PAGE_SIZE;
            for (size_t j = 0; j < region->pages; j++) {
                bitmap_clear(vma->bitmap, page + j);
            }
        }

        kfree(region);
    }

    list_free(vma->regions);

    for (uint64_t page = 0; page < vma->pages; page++) {
        if (bitmap_get(vma->bitmap, page)) {
            void *vaddr = (void *)(vma->base + page * PAGE_SIZE);
            void *pa = (void *)mmu_get_physical(pm, vaddr);
            assert(pa);
            if (--(*mmu_get_refcount(pa)) == 0)
                mmu_free(pa);
            mmu_unmap(pm, vaddr);
        }
    }

    release(&vma->lock);

    kfree(vma->bitmap);
    kfree(vma);
}

struct vma *vma_clone(struct vma *src, uintptr_t *pm) {
	if (!src || !pm)
		return NULL;

	acquire(&src->lock);

	struct vma *vma = vma_create(src->base, src->pages * PAGE_SIZE);
	memcpy(vma->bitmap, src->bitmap, ALIGN_UP(src->pages, 8) / 8);
	vma->used_pages = src->used_pages;

    foreach(i, src->regions) {
        struct vma_region *region = kmalloc(sizeof(struct vma_region));
        memcpy(region, i->value, sizeof(struct vma_region));
        list_insert(vma->regions, region);

        for (size_t j = 0; j < region->pages; j++) {
            void *vaddr = (void *)region->va + (j * PAGE_SIZE);
            void *phys = (void *)mmu_get_physical(this_proc->pm, vaddr);
            uint64_t va_flags = mmu_get_flags(this_proc->pm, vaddr);
            
            #ifdef __x86_64__
            bool writable = va_flags & PTE_WRITABLE;
            #elif __aarch64__
            bool writable = ((va_flags >> 6) & 0b11) == 0b01;
            #endif
            
            if (region->pa == 0 && writable) {
                #ifdef __x86_64__
                uint64_t flags = (va_flags & ~PTE_WRITABLE) | PTE_COW;
                #elif __aarch64__
                uint64_t flags = (va_flags & ~(0b11UL << 6)) | PTE_USER_RO | PTE_COW;
                #endif
                
                mmu_map(pm, vaddr, phys, flags);
                mmu_map(this_proc->pm, vaddr, phys, flags);
            } else {
                mmu_map(pm, vaddr, phys, va_flags);
            }
            
            (*mmu_get_refcount(phys))++;
        }

        if (region->va >= vma->base && region->va < vma->base + vma->pages * PAGE_SIZE) {
            size_t page = (region->va - vma->base) / PAGE_SIZE;
            for (size_t j = 0; j < region->pages; j++) {
                bitmap_clear(vma->bitmap, page + j);
            }
        }
    }

    for (uint64_t page = 0; page < src->pages; page++) {
        if (bitmap_get(src->bitmap, page)) {
            void *vaddr = (void *)(src->base + page * PAGE_SIZE);
            void *phys = (void *)mmu_get_physical(this_proc->pm, vaddr);
            uint64_t va_flags = mmu_get_flags(this_proc->pm, vaddr);
            
            #ifdef __x86_64__
            uint64_t flags = (va_flags & ~PTE_WRITABLE) | PTE_COW;
            #elif __aarch64__
            uint64_t flags = (va_flags & ~(0b11UL << 6)) | PTE_USER_RO | PTE_COW;
            #endif

            mmu_map(pm, vaddr, phys, flags);
            mmu_map(this_proc->pm, vaddr, phys, flags);
            
            (*mmu_get_refcount(phys))++;
        }
    }
	
    release(&src->lock);
	return vma;
}

static size_t vma_find_pages(struct vma *vma, size_t start, size_t page_count) {
    size_t pages = 0;
    size_t first_page = 0;

    for (size_t i = start; i < vma->pages; i++) {
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
                vma->last_page = first_page + page_count;
                return first_page;
            }
        } else {
            pages = 0;
        }
    }
    return (size_t)-1;
}

void *vmalloc(struct vma *vma, uintptr_t *pm, uintptr_t va, uintptr_t pa, size_t page_count, uint64_t flags) {
    acquire(&vma->lock);
    
    void *ptr;
    if (!va) {
        size_t pages = vma_find_pages(vma, vma->last_page, page_count);
        if (pages == (size_t)-1)
            pages = vma_find_pages(vma, 0, page_count);
        if (pages == (size_t)-1) {
            release(&vma->lock);
            return NULL;
        }
        ptr = (void *)(pages * PAGE_SIZE + vma->base);

        if (pa) {
            struct vma_region *region = kmalloc(sizeof(struct vma_region));
            region->pages = page_count;
            region->va = (uintptr_t)ptr;
            region->pa = pa;
            region->flags = flags;
            list_insert(vma->regions, region);
        }
    } else {
        struct vma_region *region = kmalloc(sizeof(struct vma_region));
        region->pages = page_count;
        region->va = va;
        region->pa = pa;
        region->flags = flags;
        list_insert(vma->regions, region);
        ptr = (void *)va;
    }
    for (size_t i = 0; i < page_count * PAGE_SIZE; i += PAGE_SIZE) {
        void *phys = pa ? (void *)(pa + i) : mmu_alloc();
        mmu_map(pm, ptr + i, phys, flags);
        if (!pa) memset(VIRTUAL_HHDM(phys), 0, PAGE_SIZE);
    }
    
    release(&vma->lock);
    return ptr;
}

void vfree(struct vma *vma, uintptr_t *pm, void *ptr, size_t page_count) {
    acquire(&vma->lock);
    
    foreach(i, vma->regions) {
        struct vma_region *region = i->value;
        if ((uintptr_t)ptr >= region->va && (uintptr_t)ptr < region->va + region->pages * PAGE_SIZE) {
            for (size_t j = 0; j < region->pages; j++) {
                void *vaddr = (void *)region->va + (j * PAGE_SIZE);
                if (!region->pa) {
                    void *pa = (void *)mmu_get_physical(pm, vaddr);
                    assert(pa);
                    if (--(*mmu_get_refcount(pa)) == 0)
                        mmu_free(pa);
                }
                mmu_unmap(pm, vaddr);
            }

            if (region->va >= vma->base && region->va < vma->base + vma->pages * PAGE_SIZE) {
                size_t page = (region->va - vma->base) / PAGE_SIZE;
                for (size_t j = 0; j < region->pages; j++) {
                    bitmap_clear(vma->bitmap, page + j);
                }
                vma->used_pages -= region->pages;
                if (page < vma->last_page)
                    vma->last_page = page;
            }

            list_remove(vma->regions, i);
            release(&vma->lock);
            kfree(region);
            return;
        }
    }

    if ((uintptr_t)ptr >= vma->base && (uintptr_t)ptr < vma->base + vma->pages * PAGE_SIZE) {
        size_t page = ((uintptr_t)ptr - vma->base) / PAGE_SIZE;
        for (size_t i = 0; i < page_count; i++) {
            void *vaddr = ptr + (i * PAGE_SIZE);
            void *pa = (void *)mmu_get_physical(pm, vaddr);
            assert(pa);
            if (--(*mmu_get_refcount(pa)) == 0)
                mmu_free(pa);
            mmu_unmap(pm, vaddr);
            bitmap_clear(vma->bitmap, page + i);
        }
        vma->used_pages -= page_count;
        if (page < vma->last_page)
            vma->last_page = page;
        release(&vma->lock);
        return;
    }

    release(&vma->lock);
    dprintf(LOG_WARNING, "\033[93mvma:\033[0m couldn't free region at 0x%p\n", ptr);
}