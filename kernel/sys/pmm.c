#include <stddef.h>
#include <stdint.h>
#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0  
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

uintptr_t hhdm_offset;

static uint8_t *mmu_bitmap = NULL;
static size_t   mmu_page_count = 0;
size_t mmu_usable_mem = 0;
size_t mmu_used_pages = 0;

void pmm_install(void) {
    hhdm_offset = hhdm.response->offset;

    uintptr_t highest_address = 0;

    struct limine_memmap_response *mmap = memmap_request.response;
    struct limine_memmap_entry *entry, *first_entry = NULL;

    size_t i;
    for (i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        if (!first_entry)
            first_entry = entry;

        if (entry->base + entry->length > highest_address)
            highest_address = entry->base + entry->length;
    }

    mmu_page_count = highest_address / PAGE_SIZE;
    mmu_bitmap = VIRTUAL_HHDM(first_entry->base);

    size_t bitmap_size = ALIGN_UP(mmu_page_count / 8, PAGE_SIZE);
    first_entry->base += bitmap_size;
    first_entry->length -= bitmap_size;

    memset(mmu_bitmap, 0xFF, bitmap_size);

    for (i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE)
            bitmap_clear(mmu_bitmap, (entry->base + j) / PAGE_SIZE);
        mmu_usable_mem += entry->length;
    }

    dprintf(LOG_INFO, "\033[93mmmu:\033[0m usable memory: %luK\n", mmu_usable_mem / 1024 - mmu_used_pages * 4);
}

void *mmu_alloc_frame(void) {
    for (uint64_t page = 0; page < mmu_page_count; page++) {
        if (!bitmap_get(mmu_bitmap, page)) {
            bitmap_set(mmu_bitmap, page);
            mmu_used_pages++;
            return (void *)(page * PAGE_SIZE);
        }
    }
    return NULL;
}

void mmu_free(void *ptr) {
    bitmap_clear(mmu_bitmap, (uint64_t)ptr / PAGE_SIZE);
    mmu_used_pages--;
}