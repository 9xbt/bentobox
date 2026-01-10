#include <stdbool.h>
#include <stdint.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/mmu.h>
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/sched.h>
#include <kernel/mmu.h>
#include <kernel/smp.h>
#include <limine.h>

static uintptr_t *pt_get_next_lvl(uintptr_t *lvl, uintptr_t entry, uint64_t flags, bool alloc) {
    if (lvl && lvl[entry] & PTE_PRESENT)
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
        if (pt[i] & PTE_PRESENT) {
            return false;
        }
    }
    return true;
}

static void pt_destroy(uintptr_t *pt, int lvl) {
    if (!pt || !lvl)
        return;

    int count = lvl == 4 ? 256 : 512;
    for (int i = 0; i < count; i++) {
        if (!(pt[i] & PTE_PRESENT))
            continue;

        uintptr_t *next = VIRTUAL_HHDM(PTE_GET_ADDR(pt[i]));
        if (lvl > 1) {
            pt_destroy(next, lvl - 1);
            mmu_free(PHYSICAL_HHDM(next));
        }

        pt[i] = 0;
    }
}

void tlb_invalidate(void *va) {
    asm volatile ("invlpg (%0)" ::"r"(va) : "memory");

    if (va < (void *)hhdm_offset)
        return;

    for (size_t i = 0; i < cpu_count; i++) {
        struct cpu *core = get_core(i);
        if (core == this_cpu || !core->current_tcb || core->current_tcb == core->idle_tcb)
            continue;
        // if (va < (void *)hhdm_offset && ((struct thread *)core->current_tcb->value)->parent != this_proc)
        //    continue;

        ringbuffer_write(core->tlb_invl_rb, (unsigned char *)&va, sizeof va);
        if (!__atomic_exchange_n(&core->tlb_pending, true, __ATOMIC_SEQ_CST))
            lapic_ipi(core->logical_id, 0x81);
    }
}

void mmu_switch_pm(uintptr_t *pm) {
    if (pm != kernel_pd) {
        for (int i = 256; i < 512; i++) {
            pm[i] = kernel_pd[i];
        }
    }

    asm volatile("mov %0, %%cr3" ::"r"((uint64_t)PHYSICAL_HHDM(pm)) : "memory");
}

uintptr_t *mmu_get_pm(void) {
    uint64_t pm;
    asm volatile ("mov %%cr3, %0" : "=r"(pm));
    return VIRTUAL_HHDM(pm);
}

void mmu_map_2mb(uintptr_t *pm, void *virt, void *phys, uint64_t flags) {
    assert(((uintptr_t)virt & (PAGE_SIZE_2M - 1)) == 0);

    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;
 
    uintptr_t *pdpt = pt_get_next_lvl(pm, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
 
    bool flush = pd[pd_index] & PTE_PRESENT;
    if (flags & PTE_WC)
        flags = (flags & ~PTE_PAT) | PTE_PAT_2MB;
    pd[pd_index] = (uintptr_t)phys | flags | PTE_HUGE;

    if (flush) tlb_invalidate(virt);
}

void mmu_map(uintptr_t *pm, void *virt, void *phys, uint64_t flags) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index = ((uintptr_t)virt >> 12) & 0x1ff;
    
    uintptr_t *pdpt = pt_get_next_lvl(pm, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pd = pt_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pt = pt_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);

    bool flush = pt[pt_index] & PTE_PRESENT;

    pt[pt_index] = (uintptr_t)phys | flags;
    
    if (flush) tlb_invalidate(virt);
}

void mmu_unmap_2mb(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, 0, false)) == NULL) return;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, 0, false)) == NULL) return;

    pd[pd_index] = 0;
    tlb_invalidate(virt);
        
    if (pt_empty(pd)) {
        mmu_free(PHYSICAL_HHDM(pd));
        pdpt[pdpt_index] = 0;
    }

    if (pt_empty(pdpt)) {
        mmu_free(PHYSICAL_HHDM(pdpt));
        pml4[pml4_index] = 0;
    }
}

void mmu_unmap(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd, *pt;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, 0, false)) == NULL) return;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, 0, false)) == NULL) return;
    if ((pt = pt_get_next_lvl(pd, pd_index, 0, false)) == NULL) return;

    pt[pt_index] = 0;
    tlb_invalidate(virt);

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
}

uintptr_t mmu_get_physical(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd, *pt;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, 0, false)) == NULL) return 0;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, 0, false)) == NULL) return 0;
    if ((pt = pt_get_next_lvl(pd, pd_index, 0, false)) == NULL) return 0;
    if (!(pt[pt_index] & PTE_PRESENT)) return 0;

    return (uintptr_t)PTE_GET_ADDR(pt[pt_index]) | ((uintptr_t)virt & (PAGE_SIZE - 1));
}

uint64_t mmu_get_flags(uintptr_t *pm, void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = pm, *pdpt, *pd, *pt;
    if ((pdpt = pt_get_next_lvl(pml4, pml4_index, 0, false)) == NULL) return 0;
    if ((pd = pt_get_next_lvl(pdpt, pdpt_index, 0, false)) == NULL) return 0;
    if ((pt = pt_get_next_lvl(pd, pd_index, 0, false)) == NULL) return 0;

    return PTE_GET_FLAGS(pt[pt_index]);
}

uintptr_t *mmu_create_pagemap(void) {
    uintptr_t *pm = (uintptr_t *)VIRTUAL_HHDM(mmu_alloc());
    memset(pm, 0, PAGE_SIZE);

    for (int i = 256; i < 512; i++) {
        pm[i] = kernel_pd[i];
    }

    // TODO: make it into a vma region?
    extern void signal_leave();
    mmu_map(pm, (void *)SIGNAL_TRAMPOLINE_BASE, (void *)mmu_get_physical(kernel_pd, signal_leave), PTE_PRESENT | PTE_USER);
    return pm;
}

void mmu_destroy_pagemap(uintptr_t *pm) {
    pt_destroy(pm, 4);
    mmu_free(PHYSICAL_HHDM(pm));
}