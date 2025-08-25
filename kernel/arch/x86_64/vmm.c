#include <stdbool.h>
#include <stdint.h>
#include <kernel/arch/x86_64/vmm.h>
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <limine.h>

extern char text_start_ld[];
extern char text_end_ld[];
extern char rodata_start_ld[];
extern char rodata_end_ld[];
extern char data_start_ld[];
extern char data_end_ld[];

extern volatile struct limine_memmap_request memmap_request;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request kernel_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
    .revision = 0
};

uintptr_t *kernel_pd;

static uintptr_t *pt_get_next_lvl(uintptr_t *lvl, uintptr_t entry, uint64_t flags, bool alloc) {
    if (lvl[entry] & PTE_PRESENT)
        return VIRTUAL_HHDM(PTE_GET_ADDR(lvl[entry]));
    if (!alloc)
        return NULL;

    uintptr_t *pml = (uintptr_t*)VIRTUAL_HHDM(mmu_alloc_frame());
    memset(pml, 0, PAGE_SIZE);
    lvl[entry] = (uintptr_t)PHYSICAL_HHDM(pml) | flags;
    return pml;
}

static bool pt_empty(uintptr_t *pt) {
    for (int i = 0; i < 512; i++) {
        if (pt[i] & PTE_PRESENT) {
            return false;
        }
    }
    return true;
}

void mmu_map_2mb(uintptr_t *pm, void *virt, void *phys, uint64_t flags) {
    assert(((uintptr_t)virt & (PAGE_SIZE_2M - 1)) == 0);

    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;
 
    uintptr_t *pdpt = pt_get_next_lvl(pm, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
 
    pd[pd_index] = (uintptr_t)phys | flags | PTE_HUGE;

    asm volatile ("invlpg (%0)" ::"r"(virt) : "memory");
}

void mmu_map(uintptr_t *pm, void *virt, void *phys, uint64_t flags) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index = ((uintptr_t)virt >> 12) & 0x1ff;
    
    uintptr_t *pdpt = pt_get_next_lvl(pm, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pt = pt_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);

    pt[pt_index] = (uintptr_t)phys | flags;
    
    asm volatile ("invlpg (%0)" ::"r"(virt) : "memory");
}

void mmu_unmap_2mb(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;

    pd[pd_index] = 0;
        
    if (pt_empty(pd)) {
        mmu_free(PHYSICAL_HHDM(pd));
        pdpt[pdpt_index] = 0;
    }

    if (pt_empty(pdpt)) {
        mmu_free(PHYSICAL_HHDM(pdpt));
        pml4[pml4_index] = 0;
    }

    asm volatile ("invlpg (%0)" ::"r"(virt) : "memory");
}

void mmu_unmap(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd, *pt;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;
    if ((pt = pt_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;

    pt[pt_index] = 0;

    if (pt_empty(pt)) {
        mmu_free(PHYSICAL_HHDM(pt));
        pd[pd_index] = 0;
    }

    if (pt_empty(pd)) {
        mmu_free(PHYSICAL_HHDM(pd));
        pdpt[pdpt_index] = 0;
    }

    if (pt_empty(pdpt)) {
        mmu_free(PHYSICAL_HHDM(pdpt));
        pml4[pml4_index] = 0;
    }

    asm volatile ("invlpg (%0)" ::"r"(virt) : "memory");
}

uintptr_t mmu_get_physical(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd, *pt;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if ((pt = pt_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if (!(pt[pt_index] & PTE_PRESENT)) return 0;

    return (uintptr_t)PTE_GET_ADDR(pt[pt_index]) | ((uintptr_t)virt & (PAGE_SIZE - 1));
}

uint64_t mmu_get_flags(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd, *pt;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if ((pt = pt_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if (!(pt[pt_index] & PTE_PRESENT)) return 0;

    return PTE_GET_FLAGS(pt[pt_index]);
}

void vmm_install(void) {
    kernel_pd = VIRTUAL_HHDM(mmu_alloc_frame());
    memset(kernel_pd, 0, PAGE_SIZE);

    struct limine_memmap_response *mmap = memmap_request.response;
    struct limine_memmap_entry *entry;

    for (size_t i = 0; i < mmap->entry_count; i++) {
        entry = mmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE &&
            entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE &&
            entry->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES &&
            entry->type != LIMINE_MEMMAP_FRAMEBUFFER)
            continue;
        
        size_t j, end = entry->base + entry->length;
        for (j = entry->base; j < ALIGN_UP(entry->base, PAGE_SIZE_2M) && j < end; j += PAGE_SIZE)
            mmu_map(kernel_pd, VIRTUAL_HHDM(j), (void *)j, PTE_PRESENT | PTE_WRITABLE);
        for (j = ALIGN_UP(entry->base, PAGE_SIZE_2M); j + PAGE_SIZE_2M <= ALIGN_DOWN(end, PAGE_SIZE_2M); j += PAGE_SIZE_2M)
            mmu_map_2mb(kernel_pd, VIRTUAL_HHDM(j), (void *)j, PTE_PRESENT | PTE_WRITABLE);
        for (j = ALIGN_DOWN(end, PAGE_SIZE_2M); j < end; j += PAGE_SIZE)
            mmu_map(kernel_pd, VIRTUAL_HHDM(j), (void *)j, PTE_PRESENT | PTE_WRITABLE);
    }

    uintptr_t phys_base = kernel_address_request.response->physical_base;
    uintptr_t virt_base = kernel_address_request.response->virtual_base;

    void *text_start    = (void *)ALIGN_DOWN((uintptr_t)text_start_ld, PAGE_SIZE);
    void *text_end      = (void *)ALIGN_UP((uintptr_t)text_end_ld, PAGE_SIZE);
    void *rodata_start  = (void *)ALIGN_DOWN((uintptr_t)rodata_start_ld, PAGE_SIZE);
    void *rodata_end    = (void *)ALIGN_UP((uintptr_t)rodata_end_ld, PAGE_SIZE);
    void *data_start    = (void *)ALIGN_DOWN((uintptr_t)data_start_ld, PAGE_SIZE);
    void *data_end      = (void *)ALIGN_UP((uintptr_t)data_end_ld, PAGE_SIZE);

    for (void *text = text_start; text < text_end; text += PAGE_SIZE)
        mmu_map(kernel_pd, text, text - virt_base + phys_base, PTE_PRESENT);
    for (void *rodata = rodata_start; rodata < rodata_end; rodata += PAGE_SIZE)
        mmu_map(kernel_pd, rodata, rodata - virt_base + phys_base, PTE_PRESENT | PTE_NX);
    for (void *data = data_start; data < data_end; data += PAGE_SIZE)
        mmu_map(kernel_pd, data, data - virt_base + phys_base, PTE_PRESENT | PTE_WRITABLE | PTE_NX);

    asm volatile("mov %0, %%cr3" ::"r"((uint64_t)PHYSICAL_HHDM(kernel_pd)) : "memory");

    dprintf(LOG_INFO, "\033[93mmmu:\033[0m switched to new pagemap\n");
}
