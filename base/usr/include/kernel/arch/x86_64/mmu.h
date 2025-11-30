#pragma once

#define PTE_ADDR_MASK 0x000ffffffffff000
#define PTE_GET_ADDR(x) ((x) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(x) ((x) & ~PTE_ADDR_MASK)

#define PTE_PRESENT  (1UL << 0)
#define PTE_WRITABLE (1UL << 1)
#define PTE_USER     (1UL << 2)
#define PTE_PWT      (1UL << 3)
#define PTE_PCD      (1UL << 4)
#define PTE_PAT      (1UL << 7)
#define PTE_HUGE     (1UL << 7)
#define PTE_PAT_2MB  (1UL << 12)
#define PTE_COW      (1UL << 59)
#define PTE_NX       (1UL << 63)

#define PTE_WC      (PTE_PWT | PTE_PAT)
#define PTE_WC_2MB  (PTE_PWT | PTE_PAT_2MB)