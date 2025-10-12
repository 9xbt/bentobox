#include <kernel/spinlock.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/mmu.h>

#define MSPACES 0
#define ONLY_MSPACES 0
#define FOOTERS 0
#define HAVE_MMAP 1
#define HAVE_MREMAP 0
#define HAVE_MUNMAP 1
#define HAVE_MORECORE 1
#define LACKS_TIME_H 1
#define LACKS_SYS_PARAM_H 1
#define LACKS_FCNTL_H 1
#define LACKS_UNISTD_H 1
#define LACKS_ERRNO_H 1
#define LACKS_SYS_MMAN_H 1
#define LACKS_STRING_H 0
#define LACKS_STDLIB_H 1
#define LACKS_STDIO_H 1
#define LACKS_SYS_TYPES_H 1
#define ABORT { dprintf(LOG_INFO, "%s:%d: %s: aborted\n", __FILE__, __LINE__, __func__); arch_do_backtrace(); }
#define USE_LOCKS 0
#define NO_MALLOC_STATS 1
#define LACKS_STDIO_H 1
#define MMAP_CLEARS 1
#define MALLOC_FAILURE_ACTION

#define MAP_ANONYMOUS 1

extern void arch_do_backtrace(void);

static spinlock_t lock = 0;
static uintptr_t brk = HEAP_BASE;

static void *sbrk(intptr_t increment) {
    if (!increment)
        return (void *)brk;
    
    uintptr_t current_brk = brk;
    uintptr_t new_brk = brk + increment;

    if (new_brk > current_brk) {
        for (uintptr_t page = ALIGN_UP((uintptr_t)brk, PAGE_SIZE); page < ALIGN_UP((uintptr_t)new_brk, PAGE_SIZE); page += PAGE_SIZE) {
            #ifdef __x86_64__
            uintptr_t flags = PTE_PRESENT | PTE_WRITABLE;
            #elif __aarch64__
            uintptr_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
            #endif
            mmu_map(kernel_pd, (void *)page, mmu_alloc(), flags);
        }
    } else {
        if (new_brk < HEAP_BASE)
            return (void *)-1;
        
        for (uintptr_t page = ALIGN_UP((uintptr_t)new_brk, PAGE_SIZE); page < ALIGN_UP((uintptr_t)brk, PAGE_SIZE); page += PAGE_SIZE) {
            mmu_free((void *)mmu_get_physical(kernel_pd, (void *)page));
            mmu_unmap(kernel_pd, (void *)page);
        }
    }
    
    brk = new_brk;
    return (void *)current_brk;
}

#define MORECORE sbrk

static void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    if (!length)
        return (void *)-1;

    #ifdef __x86_64__
    uintptr_t mmu_flags = PTE_PRESENT | PTE_WRITABLE;
    #elif __aarch64__
    uintptr_t mmu_flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif

    size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
    void *ptr = vmalloc(kernel_vma, kernel_pd, 0, 0, pages, mmu_flags);
    #ifdef __x86_64__
    mmu_switch_pm(mmu_get_pm());
    #endif
    return ptr;
}

static int munmap(void *addr, size_t length) {
    if (!addr || !length)
        return -EINVAL;

    size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
    vfree(kernel_vma, kernel_pd, addr, pages);
    return 0;
}

#include "../../lib/dlmalloc.c"

void *kmalloc(size_t n) {
    assert(n);
    acquire(&lock);
    void *ptr = dlmalloc(n);
    release(&lock);
    return ptr;
}

void kfree(void *ptr) {
    assert(ptr);
    acquire(&lock);
    dlfree(ptr);
    release(&lock);
}

void *krealloc(void *ptr, size_t size) {
    assert(size);
    acquire(&lock);
    void *p = dlrealloc(ptr, size);
    release(&lock);
    return p;
}