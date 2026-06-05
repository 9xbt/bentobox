#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/tar.h>
#include <limine.h>
#include <zstd.h>

void zstd_module(struct limine_file *mod) {
    dprintf(LOG_INFO, "\033[93mzstd:\033[0m decompressing %s\n", mod->path);
    const void *src = mod->address;

    unsigned long long size = ZSTD_getFrameContentSize(src, mod->size);
    if (size == ZSTD_CONTENTSIZE_ERROR) {
        dprintf(LOG_ERR, "\033[93mzstd:\033[0m not a valid zstd frame\n");
        return;
    }
    if (size == ZSTD_CONTENTSIZE_UNKNOWN) {
        dprintf(LOG_ERR, "\033[93mzstd:\033[0m content size unknown\n");
        return;
    }

    #ifdef __x86_64__
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE | PTE_NX;
    #elif __aarch64__
    uint64_t flags = PTE_VALID | PTE_AF | PTE_RW | PTE_PXN;
    #endif
    void *dest = vmalloc(kernel_vma, kernel_pd, 0, 0, ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE, flags);
    
    size_t result = ZSTD_decompress(dest, size, src, mod->size);
    if (ZSTD_isError(result)) {
        dprintf(LOG_ERR, "\033[93mzstd:\033[0m %s\n", ZSTD_getErrorName(result));
        return;
    }

    if (!memcmp(dest + 257, "ustar", 5)) {
        tar_module(dest);
    } else {
        dprintf(LOG_INFO, "\033[93mzstd:\033[0m unsupported file format\n");
        vfree(kernel_vma, kernel_pd, dest, ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE);
    }    
}