#include <errno.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/mmu.h>

#define MSPACES 1
#define ONLY_MSPACES 1
#define FOOTERS 0
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define HAVE_MUNMAP 0
#define HAVE_MORECORE 0
#define LACKS_TIME_H 1
#define LACKS_SYS_PARAM_H 1
#define LACKS_FCNTL_H 1
#define LACKS_UNISTD_H 1
#define LACKS_ERRNO_H 1
#define LACKS_SYS_MMAN_H 1
#define LACKS_STRING_H 0
#define LACKS_STDLIB_H 1
#define LACKS_STDIO_H 1
#define ABORT dprintf(LOG_INFO, "%s:%d: %s: aborted\n", __FILE__, __LINE__, __func__);
#define USE_LOCKS 0
#define NO_MALLOC_STATS 1
#define LACKS_STDIO_H 1
#define MALLOC_FAILURE_ACTION

#include "../../lib/dlmalloc.c"

#define INITIAL_HEAP_SIZE (16 * 1024 * 1024)
#define MAX_HEAP_REGIONS 64

typedef struct {
    void *base;
    size_t size;
    mspace space;
    int active;
} heap_region_t;

static heap_region_t heap_regions[MAX_HEAP_REGIONS];
static int heap_region_count = 0;

static int malloc_add_region(size_t size) {
    if (heap_region_count >= MAX_HEAP_REGIONS) return -1;

    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *base = VIRTUAL_IDENT(mmu_alloc(pages));
    mspace space = create_mspace_with_base(base, pages * PAGE_SIZE, 0);
    if (!space) return -1;

    heap_regions[heap_region_count++] = (heap_region_t){
        .base = base,
        .size = pages * PAGE_SIZE,
        .space = space,
        .active = 1
    };
    return heap_region_count - 1;
}

void malloc_initialize(void) {
    assert(malloc_add_region(INITIAL_HEAP_SIZE) >= 0);
}

void *kmalloc(size_t n) {
    for (int i = 0; i < heap_region_count; i++) {
        if (!heap_regions[i].active) continue;

        void *ptr = mspace_malloc(heap_regions[i].space, n);
        if (ptr) return ptr;
    }

    int i = malloc_add_region((n + 0x100000 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    assert(i > 0);

    return mspace_malloc(heap_regions[i].space, n);
}

void kfree(void *ptr) {
    assert(ptr != NULL);
    
    for (int i = 0; i < heap_region_count; i++) {
        if (heap_regions[i].active && 
            ptr >= heap_regions[i].base && 
            ptr < (void *)heap_regions[i].base + heap_regions[i].size) {
            mspace_free(heap_regions[i].space, ptr);
            return;
        }
    }
    dprintf(LOG_INFO, "%s:%d: double free @ 0x%p\n", __FILE__, __LINE__, ptr);
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    for (int i = 0; i < heap_region_count; i++) {
        heap_region_t *region = &heap_regions[i];
        if (!region->active) continue;

        if ((uint8_t *)ptr >= (uint8_t *)region->base &&
            (uint8_t *)ptr < (uint8_t *)region->base + region->size) {

            return mspace_realloc(region->space, ptr, size);
        }
    }

    dprintf(LOG_INFO, "%s:%d: krealloc: invalid pointer %p\n", __FILE__, __LINE__, ptr);
    assert(false);
    __builtin_unreachable();
}