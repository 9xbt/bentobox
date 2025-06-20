#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/mmu.h>
#include <kernel/acpi.h>
#include <kernel/panic.h>
#include <kernel/sched.h>
#include <kernel/printf.h>
#include <kernel/assert.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>

uintptr_t initial_pml[3][512] __attribute__((aligned(PAGE_SIZE)));
uintptr_t *kernel_pd = initial_pml[0];

extern char text_start_ld[];
extern char text_end_ld[];
extern char rodata_start_ld[];
extern char rodata_end_ld[];
extern char data_start_ld[];
extern char data_end_ld[];
extern char bss_start_ld[];
extern char bss_end_ld[];

void vmm_flush_tlb(uintptr_t virt) {
    __asm__ volatile ("invlpg (%0)" ::"r"(virt) : "memory");
}

__attribute__((no_sanitize("undefined")))
void vmm_switch_pm(uintptr_t *pm) {
    assert(pm != NULL);

    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\t" : "=r" (flags) : : "memory");
    __asm__ volatile ("cli" : : : "memory");
    __asm__ volatile("mov %0, %%cr3" ::"r"((uint64_t)PHYSICAL_IDENT(pm)) : "memory");
    this_core()->pml4 = pm;

    if (flags & (1 << 9)) __asm__ volatile ("sti" : : : "memory");
}

uintptr_t *vmm_get_next_lvl(uintptr_t *lvl, uintptr_t entry, uint64_t flags, bool alloc) {
    if (lvl[entry] & PTE_PRESENT) return VIRTUAL_IDENT(PTE_GET_ADDR(lvl[entry]));
    if (!alloc) {
        //panic("Couldn't get next PML\n");
        dprintf("%s:%d: \033[33mwarning:\033[0m couldn't get next pml\n", __FILE__, __LINE__);
        return NULL;
    }

    uintptr_t *pml = VIRTUAL_IDENT(mmu_alloc(1));
    memset(pml, 0, PAGE_SIZE);
    lvl[entry] = (uintptr_t)PHYSICAL_IDENT(pml) | flags;
    return pml;
}

void mmu_map_huge(uintptr_t virt, uintptr_t phys, uint64_t flags) {
    uintptr_t pml4_index = (virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = (virt >> 30) & 0x1ff;
    uintptr_t pd_index = (virt >> 21) & 0x1ff;
 
    uintptr_t *pdpt = vmm_get_next_lvl(this_core()->pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pd = vmm_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
 
    pd[pd_index] = phys | flags | (1 << 7);
}

void mmu_unmap_huge(uintptr_t virt) {
    uintptr_t pml4_index = (virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = (virt >> 30) & 0x1ff;
    uintptr_t pd_index   = (virt >> 21) & 0x1ff;

    uintptr_t *pml4 = this_core()->pml4, *pdpt, *pd;
    if ((pdpt = vmm_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;
    if ((pd = vmm_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;

    /* check if the page directory entry is present */
    if (pd[pd_index] & PTE_PRESENT) {
        /* clear the page directory entry */
        pd[pd_index] = 0;

        /* check if the page directory is empty */
        bool pd_empty = true;
        for (int i = 0; i < 512; i++) {
            if (pd[i] & PTE_PRESENT) {
                pd_empty = false;
                break;
            }
        }

        /* free it if it's empty */
        if (pd_empty) {
            mmu_free(pd, 1);
            pdpt[pdpt_index] = 0;
        }
    }

    /* check if the page directory pointer table entry is present */
    bool pdpt_empty = true;
    for (int i = 0; i < 512; i++) {
        if (pdpt[i] & PTE_PRESENT) {
            pdpt_empty = false;
            break;
        }
    }

    /* free it if it's empty */
    if (pdpt_empty) {
        mmu_free(pdpt, 1);
        pml4[pml4_index] = 0;
    }

    vmm_flush_tlb(virt);
}

__attribute__((no_sanitize("undefined")))
void mmu_map(void *virt, void *phys, uint64_t flags) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index = ((uintptr_t)virt >> 12) & 0x1ff;
    
    uintptr_t *pdpt = vmm_get_next_lvl(this_core()->pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pd = vmm_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);
    uintptr_t *pt = vmm_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, true);

    pt[pt_index] = (uintptr_t)phys | flags; /* map the page */
    
    vmm_flush_tlb((uintptr_t)virt); /* flush the tlb entry */
}

void mmu_unmap(void *virt) {
    uintptr_t pml4_index = ((uintptr_t)virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = ((uintptr_t)virt >> 30) & 0x1ff;
    uintptr_t pd_index   = ((uintptr_t)virt >> 21) & 0x1ff;
    uintptr_t pt_index   = ((uintptr_t)virt >> 12) & 0x1ff;

    uintptr_t *pml4 = this_core()->pml4, *pdpt, *pd, *pt;
    if ((pdpt = vmm_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;
    if ((pd = vmm_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;
    if ((pt = vmm_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return;

    pt[pt_index] = 0;

    /* check if the page table entry is present */
    bool pt_empty = true;
    for (int i = 0; i < 512; i++) {
        if (pt[i] & PTE_PRESENT) {
            pt_empty = false;
            break;
        }
    }

    /* free it if it's empty */
    if (pt_empty) {
        mmu_free(PHYSICAL_IDENT(pt), 1);
        pd[pd_index] = 0;
    }

    /* check if the page directory entry is present */
    bool pd_empty = true;
    for (int i = 0; i < 512; i++) {
        if (pd[i] & PTE_PRESENT) {
            pd_empty = false;
            break;
        }
    }

    /* free it if it's empty */
    if (pd_empty) {
        mmu_free(PHYSICAL_IDENT(pd), 1);
        pdpt[pdpt_index] = 0;
    }

    /* check if the page directory pointer table entry is present */
    bool pdpt_empty = true;
    for (int i = 0; i < 512; i++) {
        if (pdpt[i] & PTE_PRESENT) {
            pdpt_empty = false;
            break;
        }
    }

    /* free it if it's empty */
    if (pdpt_empty) {
        mmu_free(PHYSICAL_IDENT(pdpt), 1);
        pml4[pml4_index] = 0;
    }

    vmm_flush_tlb((uintptr_t)virt);
}

void mmu_map_pages(size_t count, void *virt, void *phys, uint64_t flags) {
    for (uint32_t i = 0; i < count * PAGE_SIZE; i += PAGE_SIZE) {
        mmu_map((void *)((uintptr_t)virt + i), (void *)((uintptr_t)phys + i), flags);
    }
}

void mmu_unmap_pages(size_t count, void *virt) {
    for (uint32_t i = 0; i < count * PAGE_SIZE; i += PAGE_SIZE) {
        mmu_unmap((void *)((uintptr_t)virt + i));
    }
}

uintptr_t mmu_get_physical(uintptr_t *pml4, uintptr_t virt) {
    uintptr_t pml4_index = (virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = (virt >> 30) & 0x1ff;
    uintptr_t pd_index = (virt >> 21) & 0x1ff;
    uintptr_t pt_index = (virt >> 12) & 0x1ff;

    uintptr_t *pdpt, *pd, *pt;
    if ((pdpt = vmm_get_next_lvl(pml4, pml4_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if ((pd = vmm_get_next_lvl(pdpt, pdpt_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;
    if ((pt = vmm_get_next_lvl(pd, pd_index, PTE_PRESENT | PTE_WRITABLE | PTE_USER, false)) == NULL) return 0;

    return (uintptr_t)PTE_GET_ADDR(pt[pt_index]) | (virt & (PAGE_SIZE - 1));
}

void mmu_free_page_table(uintptr_t *table, int level) {
    if (level == 0 || !table) return;

    int count = level == 4 ? 256 : 512;
    for (int i = 0; i < count; i++) {
        if (!(table[i] & PTE_PRESENT))
            continue;

        uintptr_t entry = table[i];

        if (level == 2 && (entry & (1 << 7))) {
            /* 2 MiB huge page */
            table[i] = 0;
            continue;
        }

        uintptr_t *next = (uintptr_t *)PTE_GET_ADDR(entry);
        if (level > 1) {
            mmu_free_page_table(next, level - 1);
            mmu_free(next, 1);
        }

        table[i] = 0;
    }
}

uintptr_t *mmu_create_user_pm(struct task *proc) {
    uintptr_t *pml4 = (uintptr_t *)VIRTUAL_IDENT(mmu_alloc(1));
    memset(pml4, 0, PAGE_SIZE);
    
    this_core()->pml4 = pml4;
    for (int i = 256; i < 512; i++) {
        pml4[i] = kernel_pd[i];
    }
    mmu_map_huge(0x000000, 0x000000, PTE_PRESENT | PTE_WRITABLE);
    mmu_map_huge(0x200000, 0x200000, PTE_PRESENT | PTE_WRITABLE);

    return pml4;
}

void mmu_destroy_user_pm(uintptr_t *pml4) {
    mmu_free_page_table(pml4, 4);
    mmu_free(PHYSICAL_IDENT(pml4), 1);
}

void vmm_direct_map_huge(uintptr_t *pml4, uintptr_t virt, uintptr_t phys, uint64_t flags) {
    uintptr_t pml4_index = (virt >> 39) & 0x1ff;
    uintptr_t pdpt_index = (virt >> 30) & 0x1ff;
    uintptr_t pd_index   = (virt >> 21) & 0x1ff;
    
    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        uintptr_t *pdpt = (uintptr_t *)mmu_alloc(1);
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_index] = (uintptr_t)pdpt | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    uintptr_t *pdpt = (uintptr_t *)PTE_GET_ADDR(pml4[pml4_index]);
    if (!(pdpt[pdpt_index] & PTE_PRESENT)) {
        uintptr_t *pd = (uintptr_t *)mmu_alloc(1);
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_index] = (uintptr_t)pd | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    
    uintptr_t *pd = (uintptr_t *)PTE_GET_ADDR(pdpt[pdpt_index]);
    
    pd[pd_index] = phys | flags | (1 << 7);
}

void vmm_install(void) {
    for (uintptr_t addr = 0x0; addr < mmu_page_count * PAGE_SIZE /* 256MiB */; addr += 0x200000)
        vmm_direct_map_huge(kernel_pd, (uintptr_t)VIRTUAL_IDENT(addr), addr, PTE_PRESENT | PTE_WRITABLE);

    kernel_pd = (uintptr_t *)VIRTUAL_IDENT(mmu_alloc(1));
    this_core()->pml4 = kernel_pd;
    memcpy(kernel_pd, initial_pml[0], PAGE_SIZE);

    dprintf("%s:%d: done mapping kernel regions\n", __FILE__, __LINE__);

    vmm_switch_pm(kernel_pd);
    dprintf("%s:%d: successfully switched page tables\n", __FILE__, __LINE__);
}