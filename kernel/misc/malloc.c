#include <kernel/mmu.h>
#include <kernel/panic.h>
#include <kernel/errno.h>
#include <kernel/string.h>

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
#define ABORT panic("Allocation aborted!");
#define MSPACES 1
#define ONLY_MSPACES 1
#define FOOTERS 0
#define USE_LOCKS 0
#define NO_MALLOC_STATS 1
#define LACKS_STDIO_H 1
#define MALLOC_FAILURE_ACTION panic("Allocation failed!")

#include <kernel/dlmalloc.c>

#define HEAP_SIZE 16 * 1024 * 1024

static void *heap_start = NULL;
static mspace kspace;

void malloc_initialize() {
    heap_start = VIRTUAL_IDENT(mmu_alloc(HEAP_SIZE / PAGE_SIZE));
    kspace = create_mspace_with_base(heap_start, HEAP_SIZE, 0);
}

void *kmalloc(size_t n) {
    return mspace_malloc(kspace, n);
}

void kfree(void *ptr) {
    mspace_free(kspace, ptr);
}