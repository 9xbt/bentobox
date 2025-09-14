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

    uintptr_t *pml = (uintptr_t*)VIRTUAL_HHDM(mmu_alloc());
    memset(pml, 0, PAGE_SIZE);
    lvl[entry] = (uintptr_t)PHYSICAL_HHDM(pml) | flags;
    return pml;
}

static bool pt_empty(uintptr_t *pt) {
    for (int i = 0; i < 512; i++) {
        if (pt[i] & PTE_VALID) {
            return false;
        }
    }
    return true;
}

static inline void tlb_invalidate(void *va) {
    asm volatile("tlbi vae1, %0" : : "r"((uintptr_t)va >> 12) : "memory");
    asm volatile("dsb ish; isb");
}

void mmu_switch_pm(uintptr_t *pm) {
    asm volatile("msr ttbr0_el1, %0" : : "r"(PHYSICAL_HHDM(pm)) : "memory");
    if (pm == kernel_pd) {
        asm volatile("msr ttbr1_el1, %0" : : "r"(PHYSICAL_HHDM(pm)) : "memory");
        asm volatile("dsb ish; tlbi vmalle1; dsb ish; isb" : : : "memory");
    } else {
        asm volatile("dsb ish; tlbi vmalle1; dsb ish; isb" : : : "memory");
    }
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
    virt = (void *)((uintptr_t)virt & ~(PAGE_SIZE - 1));

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

void mmu_unmap_2mb(uintptr_t *pm, void *virt) {
    assert(((uintptr_t)virt & (PAGE_SIZE_2M - 1)) == 0);

    uintptr_t l0_index = ((uintptr_t)virt >> 39) & 0x1FF;
    uintptr_t l1_index = ((uintptr_t)virt >> 30) & 0x1FF;
    uintptr_t l2_index = ((uintptr_t)virt >> 21) & 0x1FF;

    uintptr_t *l0 = pm, *l1, *l2;
    if ((l1 = pt_get_next_lvl(l0, l0_index, 0, false)) == NULL) return;
    if ((l2 = pt_get_next_lvl(l1, l1_index, 0, false)) == NULL) return;

    l2[l2_index] = 0;
        
    if (pt_empty(l2)) {
        mmu_free(PHYSICAL_HHDM(l2));
        l1[l1_index] = 0;
    }

    if (pt_empty(l1)) {
        mmu_free(PHYSICAL_HHDM(l1));
        l0[l0_index] = 0;
    }

    tlb_invalidate(virt);
}

void mmu_unmap(uintptr_t *pm, void *virt) {
    assert(((uintptr_t)virt & (PAGE_SIZE - 1)) == 0);

    uintptr_t l0_index = ((uintptr_t)virt >> 39) & 0x1FF;
    uintptr_t l1_index = ((uintptr_t)virt >> 30) & 0x1FF;
    uintptr_t l2_index = ((uintptr_t)virt >> 21) & 0x1FF;
    uintptr_t l3_index = ((uintptr_t)virt >> 12) & 0x1FF;

    uintptr_t *l0 = pm, *l1, *l2, *l3;
    if ((l1 = pt_get_next_lvl(l0, l0_index, 0, false)) == NULL) return;
    if ((l2 = pt_get_next_lvl(l1, l1_index, 0, false)) == NULL) return;
    if ((l3 = pt_get_next_lvl(l2, l2_index, 0, false)) == NULL) return;

    l3[l3_index] = 0;

    if (pt_empty(l3)) {
        mmu_free(PHYSICAL_HHDM(l3));
        l2[l2_index] = 0;
    }

    if (pt_empty(l2)) {
        mmu_free(PHYSICAL_HHDM(l2));
        l1[l1_index] = 0;
    }

    if (pt_empty(l1)) {
        mmu_free(PHYSICAL_HHDM(l1));
        l0[l0_index] = 0;
    }

    tlb_invalidate(virt);
}

uintptr_t mmu_get_physical(uintptr_t *pm, void *virt) {
    assert(((uintptr_t)virt & (PAGE_SIZE - 1)) == 0);

    uintptr_t l0_index = ((uintptr_t)virt >> 39) & 0x1FF;
    uintptr_t l1_index = ((uintptr_t)virt >> 30) & 0x1FF;
    uintptr_t l2_index = ((uintptr_t)virt >> 21) & 0x1FF;
    uintptr_t l3_index = ((uintptr_t)virt >> 12) & 0x1FF;

    uintptr_t *l0 = pm, *l1, *l2, *l3;
    if ((l1 = pt_get_next_lvl(l0, l0_index, 0, false)) == NULL) return 0;
    if ((l2 = pt_get_next_lvl(l1, l1_index, 0, false)) == NULL) return 0;
    if ((l3 = pt_get_next_lvl(l2, l2_index, 0, false)) == NULL) return 0;

    return (uintptr_t)PTE_GET_ADDR(l3[l3_index]) | ((uintptr_t)virt & (PAGE_SIZE - 1));
}

uint64_t mmu_get_flags(uintptr_t *pm, void *virt) {
    virt = (void *)((uintptr_t)virt & ~(PAGE_SIZE - 1));

    uintptr_t l0_index = ((uintptr_t)virt >> 39) & 0x1FF;
    uintptr_t l1_index = ((uintptr_t)virt >> 30) & 0x1FF;
    uintptr_t l2_index = ((uintptr_t)virt >> 21) & 0x1FF;
    uintptr_t l3_index = ((uintptr_t)virt >> 12) & 0x1FF;

    uintptr_t *l0 = pm, *l1, *l2, *l3;
    if ((l1 = pt_get_next_lvl(l0, l0_index, 0, false)) == NULL) return 0;
    if ((l2 = pt_get_next_lvl(l1, l1_index, 0, false)) == NULL) return 0;
    if ((l3 = pt_get_next_lvl(l2, l2_index, 0, false)) == NULL) return 0;

    return PTE_GET_FLAGS(l3[l3_index]);
}

uintptr_t *mmu_create_pagemap(void) {
    uintptr_t *pm = (uintptr_t *)VIRTUAL_HHDM(mmu_alloc());
    memset(pm, 0, PAGE_SIZE);

    for (int i = 256; i < 512; i++) {
        pm[i] = kernel_pd[i];
    }

    return pm;
}