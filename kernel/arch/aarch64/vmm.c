#include <stdbool.h>
#include <stdint.h>
#include <kernel/arch/aarch64/mmu.h>
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/mmu.h>
#include <limine.h>

static uintptr_t *pt_get_next_lvl(uintptr_t *lvl, uintptr_t entry, uint64_t flags, bool alloc) {
    if ((lvl[entry] & PTE_VALID) && (lvl[entry] & PTE_TABLE))
        return VIRTUAL_HHDM(PTE_GET_ADDR(lvl[entry]));
    if (!alloc)
        return NULL;

    uintptr_t *pml = (uintptr_t*)VIRTUAL_HHDM(mmu_alloc_frame());
    memset(pml, 0, PAGE_SIZE);
    lvl[entry] = (uintptr_t)PHYSICAL_HHDM(pml) | flags;
    return pml;
}

static inline void tlb_invalidate(void *va) {
    asm volatile("tlbi vae1, %0" : : "r"((uintptr_t)va >> 12) : "memory");
    asm volatile("dsb ish; isb");
}

void mmu_switch_pm(uintptr_t *pm) {
    asm volatile("msr ttbr0_el1, %0" : : "r"(PHYSICAL_HHDM(pm)) : "memory");
    asm volatile("msr ttbr1_el1, %0" : : "r"(PHYSICAL_HHDM(pm)) : "memory");
    asm volatile("dsb ish; tlbi vmalle1; dsb ish; isb" : : : "memory");
}

void mmu_map_2mb(uintptr_t *pm, void *virt, void *phys, uint64_t flags) {
    assert(((uintptr_t)virt & (PAGE_SIZE_2M - 1)) == 0);

    uintptr_t l0_index = ((uintptr_t)virt >> 39) & 0x1FF;
    uintptr_t l1_index = ((uintptr_t)virt >> 30) & 0x1FF;
    uintptr_t l2_index = ((uintptr_t)virt >> 21) & 0x1FF;

    uintptr_t *l1 = pt_get_next_lvl(pm, l0_index, PTE_VALID | PTE_TABLE, true);
    uintptr_t *l2 = pt_get_next_lvl(l1, l1_index, PTE_VALID | PTE_TABLE, true);

    l2[l2_index] = ((uintptr_t)phys & ~0x1FFFFFUL) | flags | PTE_BLOCK;

    tlb_invalidate(virt);
}

void mmu_map(uintptr_t *pm, void *virt, void *phys, uint64_t flags) {
    assert(((uintptr_t)virt & (PAGE_SIZE - 1)) == 0);

    uintptr_t l0_index = ((uintptr_t)virt >> 39) & 0x1FF;
    uintptr_t l1_index = ((uintptr_t)virt >> 30) & 0x1FF;
    uintptr_t l2_index = ((uintptr_t)virt >> 21) & 0x1FF;
    uintptr_t l3_index = ((uintptr_t)virt >> 12) & 0x1FF;

    uintptr_t *l1 = pt_get_next_lvl(pm, l0_index, PTE_VALID | PTE_TABLE, true);
    uintptr_t *l2 = pt_get_next_lvl(l1, l1_index, PTE_VALID | PTE_TABLE, true);
    uintptr_t *l3 = pt_get_next_lvl(l2, l2_index, PTE_VALID | PTE_TABLE, true);

    l3[l3_index] = ((uintptr_t)phys & ~0xFFFUL) | flags | PTE_4K_PAGE;
    tlb_invalidate(virt);
}

void mmu_unmap_2mb(uintptr_t *pm, void *virt) {}
void mmu_unmap(uintptr_t *pm, void *virt) {}
uintptr_t mmu_get_physical(uintptr_t *pm, void *virt) {}
uint64_t mmu_get_flags(uintptr_t *pm, void *virt) {}