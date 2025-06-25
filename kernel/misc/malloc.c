#include <kernel/mmu.h>
#include <kernel/panic.h>
#include <kernel/errno.h>
#include <kernel/string.h>
#include <kernel/printf.h>

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
#define ABORT dprintf("%s:%d: %s: aborted\n", __FILE__, __LINE__, __func__);
#define USE_LOCKS 0
#define NO_MALLOC_STATS 1
#define LACKS_STDIO_H 1
#define MALLOC_FAILURE_ACTION

#include <kernel/dlmalloc.c>

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
static size_t total_allocated = 0;

static int add_heap_region(size_t size) {
    if (heap_region_count >= MAX_HEAP_REGIONS) {
        return -1;
    }
    
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *base = VIRTUAL_IDENT(mmu_alloc(pages));

    mspace space = create_mspace_with_base(base, pages * PAGE_SIZE, 0);
    if (!space) {
        mmu_free(base, pages);
        return -1;
    }
    
    heap_regions[heap_region_count].base = base;
    heap_regions[heap_region_count].size = pages * PAGE_SIZE;
    heap_regions[heap_region_count].space = space;
    heap_regions[heap_region_count].active = 1;
    
    total_allocated += pages * PAGE_SIZE;
    heap_region_count++;
    return heap_region_count - 1;
}

void malloc_initialize() {
    if (add_heap_region(INITIAL_HEAP_SIZE) < 0) {
        panic("Failed to initialize heap");
    }
}

void *kmalloc(size_t n) {
    int i;
    for (i = 0; i < heap_region_count; i++) {
        if (heap_regions[i].active) {
            void *ptr = mspace_malloc(heap_regions[i].space, n);
            if (ptr != NULL) {
                return ptr;
            }
        }
    }
    
    i = add_heap_region((((n + 1048576) + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE);
    if (i < 0) {
        panic("Failed to expand heap: out of memory");
        return NULL;
    }
    
    void *ptr = mspace_malloc(heap_regions[i].space, n);
    if (!ptr) {
        panic("Allocation failed even after heap expansion");
    }
    return ptr;
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        dprintf("%s:%d: invalid deallocation @ 0x%p\n", ptr);
        return;
    }
    
    for (int i = 0; i < heap_region_count; i++) {
        if (heap_regions[i].active && 
            ptr >= heap_regions[i].base && 
            ptr < (void *)heap_regions[i].base + heap_regions[i].size) {
            mspace_free(heap_regions[i].space, ptr);
            return;
        }
    }
    dprintf("%s:%d: double free @ 0x%p\n", ptr);
}