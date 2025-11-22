#include <stddef.h>
#include <stdint.h>
#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/bitmap.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kernel_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
    .revision = 0
};

extern char text_start_ld[];
extern char text_end_ld[];
extern char rodata_start_ld[];
extern char rodata_end_ld[];
extern char data_start_ld[];
extern char data_end_ld[];

uintptr_t  *kernel_pd;
struct vma *kernel_vma;

uintptr_t hhdm_offset;

static uint8_t  *mmu_bitmap = NULL;
static uint16_t *mmu_refcounts = NULL;
static size_t    mmu_page_count = 0;
size_t mmu_usable_mem = 0;
size_t mmu_used_pages = 0;

static uintptr_t module_base = 0;

void mmu_initialize(void) {
    hhdm_offset = hhdm.response->offset;
    module_base = ALIGN_UP((uintptr_t)data_end_ld, PAGE_SIZE);

    uintptr_t highest_address = 0;

    struct limine_memmap_response *mmap = memmap_request.response;
    struct limine_memmap_entry *entry, *bitmap_entry = NULL;

    size_t i;
    for (i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE &&
            entry->base + entry->length > highest_address)
            highest_address = entry->base + entry->length;
    }

    mmu_page_count = highest_address / PAGE_SIZE;
    size_t bitmap_size = ALIGN_UP(mmu_page_count / 8, PAGE_SIZE);

    for (i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];
        
        if (entry->type == LIMINE_MEMMAP_USABLE &&
            entry->length >= bitmap_size)
            bitmap_entry = entry;
    }

    mmu_bitmap = VIRTUAL_HHDM(bitmap_entry->base);
    bitmap_entry->base += bitmap_size;
    bitmap_entry->length -= bitmap_size;

    memset(mmu_bitmap, 0xFF, bitmap_size);

    for (i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE)
            bitmap_clear(mmu_bitmap, (entry->base + j) / PAGE_SIZE);
        mmu_usable_mem += entry->length;
    }

    bitmap_entry->base -= bitmap_size;
    bitmap_entry->length += bitmap_size;

    dprintf(LOG_INFO, "\033[93mmmu:\033[0m usable memory: %luK\n", mmu_usable_mem / 1024);

    kernel_pd = VIRTUAL_HHDM(mmu_alloc());
    memset(kernel_pd, 0, PAGE_SIZE);

    for (i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE &&
            entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE &&
            entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES &&
            entry->type != LIMINE_MEMMAP_FRAMEBUFFER &&
            entry->type != LIMINE_MEMMAP_ACPI_RECLAIMABLE)
            continue;

        size_t j, end = entry->base + entry->length;
        #ifdef __x86_64__
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        #elif __aarch64__
        uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
        #endif
        for (j = entry->base; j < ALIGN_UP(entry->base, PAGE_SIZE_2M) && j < end; j += PAGE_SIZE)
            mmu_map(kernel_pd, VIRTUAL_HHDM(j), (void *)j, flags);
        for (j = ALIGN_UP(entry->base, PAGE_SIZE_2M); j + PAGE_SIZE_2M <= ALIGN_DOWN(end, PAGE_SIZE_2M); j += PAGE_SIZE_2M)
            mmu_map_2mb(kernel_pd, VIRTUAL_HHDM(j), (void *)j, flags);
        for (j = ALIGN_DOWN(end, PAGE_SIZE_2M); j < end; j += PAGE_SIZE)
            mmu_map(kernel_pd, VIRTUAL_HHDM(j), (void *)j, flags);
    }

    uintptr_t phys_base = kernel_address_request.response->physical_base;
    uintptr_t virt_base = kernel_address_request.response->virtual_base;

    void *text_start    = (void *)ALIGN_DOWN((uintptr_t)text_start_ld, PAGE_SIZE);
    void *text_end      = (void *)ALIGN_UP((uintptr_t)text_end_ld, PAGE_SIZE);
    void *rodata_start  = (void *)ALIGN_DOWN((uintptr_t)rodata_start_ld, PAGE_SIZE);
    void *rodata_end    = (void *)ALIGN_UP((uintptr_t)rodata_end_ld, PAGE_SIZE);
    void *data_start    = (void *)ALIGN_DOWN((uintptr_t)data_start_ld, PAGE_SIZE);
    void *data_end      = (void *)ALIGN_UP((uintptr_t)data_end_ld, PAGE_SIZE);

    #ifdef __x86_64__
    uint64_t text_flags = PTE_PRESENT;
    uint64_t rodata_flags = PTE_PRESENT | PTE_NX;
    uint64_t data_flags = PTE_PRESENT | PTE_WRITABLE | PTE_NX;
    #elif __aarch64__
    uint64_t text_flags   = PTE_VALID | PTE_AF;
    uint64_t rodata_flags = PTE_VALID | PTE_AF | PTE_PXN;
    uint64_t data_flags   = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    for (void *text = text_start; text < text_end; text += PAGE_SIZE)
        mmu_map(kernel_pd, text, text - virt_base + phys_base, text_flags);
    for (void *rodata = rodata_start; rodata < rodata_end; rodata += PAGE_SIZE)
        mmu_map(kernel_pd, rodata, rodata - virt_base + phys_base, rodata_flags);
    for (void *data = data_start; data < data_end; data += PAGE_SIZE)
        mmu_map(kernel_pd, data, data - virt_base + phys_base, data_flags);

    mmu_switch_pm(kernel_pd);

    dprintf(LOG_INFO, "\033[93mmmu:\033[0m switched to new pagemap\n");

    kernel_vma = vma_create(VMA_KERNEL_BASE, 256 * 1024 * 1024);
    mmu_refcounts = vmalloc(kernel_vma, kernel_pd, 0, 0, ALIGN_UP(mmu_page_count * sizeof(uint16_t), PAGE_SIZE) / PAGE_SIZE, data_flags);
}

static uint64_t last_page = 0;
static spinlock_t lock = 0;

uint64_t mmu_find_page(uint64_t start) {
    for (uint64_t page = start; page < mmu_page_count; page++) {
        if (!bitmap_get(mmu_bitmap, page)) {
            bitmap_set(mmu_bitmap, page);
            mmu_used_pages++;
            last_page = page + 1;
            return page;
        }
    }
    return (uint64_t)-1;
}

void *mmu_alloc(void) {
    acquire(&lock);
    uint64_t page = mmu_find_page(last_page);
    if (page == (uint64_t)-1)
        page = mmu_find_page(0);
    if (page == (uint64_t)-1)
        panic("Out of memory");
    if (mmu_refcounts)
        mmu_refcounts[page] = 1;
    release(&lock);
    return (void *)(page * PAGE_SIZE);
}

void mmu_free(void *ptr) {
    assert(ptr);
    uint64_t page = (uint64_t)ptr / PAGE_SIZE;

    acquire(&lock);
    if (!bitmap_get(mmu_bitmap, page)) {
        dprintf(LOG_DEBUG, "\033[93mmmu:\033[0m potential double free at 0x%p\n", ptr);
        return; 
    }

    bitmap_clear(mmu_bitmap, page);
    mmu_used_pages--;

    if (page < last_page)
        last_page = (uint64_t)ptr / PAGE_SIZE;
    release(&lock);
}

uint16_t *mmu_get_refcount(void *ptr) {
    assert(ptr);
    if (!mmu_refcounts)
        return NULL;
    uint64_t page = (uint64_t)ptr / PAGE_SIZE;
    return &mmu_refcounts[page];
}

void *mmu_map_module(uintptr_t base, size_t len) {
    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW;
    #endif

    len = ALIGN_UP(len, PAGE_SIZE);
    for (uint32_t i = 0; i < len; i += PAGE_SIZE) {
        mmu_map(kernel_pd, (void *)((uintptr_t)module_base + i), (void *)(mmu_get_physical(kernel_pd, (void *)base + i)), flags);
    }

    module_base += len;
    return (void *)(module_base - len);
}

void *mmu_map_module_bss(size_t pages) {
    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    size_t length = pages * PAGE_SIZE;
    for (size_t page = 0; page < pages; page++) {
        void *paddr = mmu_alloc();
        void *vaddr = (void *)(module_base + page * PAGE_SIZE);

        mmu_map(kernel_pd, vaddr, paddr, flags);
        memset(vaddr, 0, PAGE_SIZE);
    }

    module_base += length;
    return (void *)(module_base - length);
}

void mmu_print_memory(void) {
    dprintf(LOG_DEBUG, "\033[93mmmu:\033[0m %luK/%luK\n", mmu_used_pages * 4, mmu_usable_mem / 1024);
}