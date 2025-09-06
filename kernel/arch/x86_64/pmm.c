#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/multiboot.h>
#include <kernel/spinlock.h>
#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/mmu.h>

uint8_t *mmu_bitmap = NULL;
uint64_t mmu_bitmap_size = 0;
uint64_t mmu_page_count = 0;
uint64_t mmu_usable_mem = 0;
uint64_t mmu_used_pages = 0;

atomic_flag pmm_lock = ATOMIC_FLAG_INIT;

void pmm_install(void) {
    extern void *mboot;
    extern char end[];
    uintptr_t highest_address = 0;
    uint32_t mboot_size = *(uint32_t *)mboot;

    struct multiboot_tag_mmap *mmap = mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_MMAP);
    struct multiboot_mmap_entry *mmmt = NULL;

    uint32_t i;
    for (i = 0; i < (mmap->size - sizeof(struct multiboot_tag_mmap)) / mmap->entry_size; i++) {
        mmmt = &mmap->entries[i];
        
        if (mmmt->addr < 0x100000) {
            mmmt->type = MULTIBOOT_MEMORY_RESERVED;
            continue;
        }

        if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
            if (mmmt->addr >= 0x100000 && mmmt->addr < (uintptr_t)&end) {
                mmmt->len -= (uintptr_t)&end - 0x100000;
                mmmt->addr = (uintptr_t)&end;
            }

            if (mmmt->addr + mmmt->len > highest_address)
                highest_address = mmmt->addr + mmmt->len;
        }
    }

    mmu_bitmap = (uint8_t *)&end;

    if ((void *)mmu_bitmap < mboot + mboot_size)
        mmu_bitmap = (uint8_t *)ALIGN_UP((uintptr_t)mboot + mboot_size, PAGE_SIZE);

    struct multiboot_tag_module *mod = mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_MODULE);
    while (mod) {
        if (ALIGN_UP(mod->mod_end, PAGE_SIZE) > (uintptr_t)mmu_bitmap)
            mmu_bitmap = (uint8_t *)ALIGN_UP((uintptr_t)mod->mod_end, PAGE_SIZE);
        mod = mboot2_find_next((char *)mod + ALIGN_UP(mod->size, 8), MULTIBOOT_TAG_TYPE_MODULE);
    }

    mmu_page_count = highest_address / PAGE_SIZE;
    mmu_bitmap_size = ALIGN_UP(mmu_page_count / 8, PAGE_SIZE);
    memset(mmu_bitmap, 0xFF, mmu_bitmap_size);

    for (i = 0; i < (mmap->size - sizeof(struct multiboot_tag_mmap)) / mmap->entry_size; i++) {
        mmmt = &mmap->entries[i];

        if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
            for (uint64_t j = 0; j < mmmt->len; j += PAGE_SIZE) {
                bitmap_clear(mmu_bitmap, (mmmt->addr + j) / PAGE_SIZE);
            }
            mmu_usable_mem += mmmt->len;
        }
    }

    mmu_mark_used(mmu_bitmap, mmu_bitmap_size / PAGE_SIZE);
    mmu_mark_used(mboot, ALIGN_UP(mboot_size, PAGE_SIZE) / PAGE_SIZE);

    mod = mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_MODULE);
    while (mod) {
        mmu_mark_used((void *)(uintptr_t)mod->mod_start, ALIGN_UP(mod->mod_end - mod->mod_start, PAGE_SIZE) / PAGE_SIZE);
        mod = mboot2_find_next((char *)mod + ALIGN_UP(mod->size, 8), MULTIBOOT_TAG_TYPE_MODULE);
    }
    
    dprintf(LOG_INFO, "%s:%d: bitmap address: 0x%p\n", __FILE__, __LINE__, (uint64_t)mmu_bitmap);
    dprintf(LOG_INFO, "%s:%d: usable memory: %luK\n", __FILE__, __LINE__, mmu_usable_mem / 1024 - mmu_used_pages * 4);
}

void mmu_mark_used(void *ptr, size_t page_count) {
    for (size_t i = 0; i < page_count * PAGE_SIZE; i += PAGE_SIZE) {
        bitmap_set(mmu_bitmap, ((uintptr_t)ptr + i) / PAGE_SIZE);
    }
    mmu_used_pages += page_count;
}

uint64_t mmu_find_pages(uint64_t page_count) {
    uint64_t pages = 0;
    uint64_t first_page = 0;

    for (uint64_t i = 0; i < mmu_page_count; i++) {
        if (!bitmap_get(mmu_bitmap, i)) {
            if (pages == 0) {
                first_page = i;
            }
            pages++;
            if (pages == page_count) {
                for (uint64_t j = 0; j < page_count; j++) {
                    acquire(&pmm_lock);
                    bitmap_set(mmu_bitmap, first_page + j);
                    release(&pmm_lock);
                }

                mmu_used_pages += page_count;
                return first_page;
            }
        } else {
            pages = 0;
        }
    }
    return 0;
}

void *mmu_alloc(size_t page_count) {
    uint64_t pages = mmu_find_pages(page_count);
    if (!pages) {
        panic("allocation failed: out of memory");
    }
    return (void *)(pages * PAGE_SIZE);
}

void mmu_free(void *ptr, size_t page_count) {
    uint64_t page = (uint64_t)ptr / PAGE_SIZE;

    if ((uintptr_t)ptr < 0x100000 || page > mmu_bitmap_size * 8) {
        dprintf(LOG_INFO, "%s:%d: invalid deallocation @ 0x%p\n", __FILE__, __LINE__, ptr);
        return;
    }

    acquire(&pmm_lock);
    for (uint64_t i = 0; i < page_count; i++) {
        if (!bitmap_get(mmu_bitmap, page + i)) {
            dprintf(LOG_INFO, "%s:%d: double free @ 0x%p\n", __FILE__, __LINE__, ptr);
            release(&pmm_lock);
            return;
        }
        bitmap_clear(mmu_bitmap, page + i);
    }
    release(&pmm_lock);
    
    mmu_used_pages -= page_count;
}