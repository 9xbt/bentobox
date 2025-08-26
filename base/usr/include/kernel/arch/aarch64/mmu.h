#pragma once

#define PTE_ADDR_MASK 0x000ffffffffff000
#define PTE_GET_ADDR(x) ((x) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(x) ((x) & ~PTE_ADDR_MASK)

#define PTE_VALID   (1UL << 0)
#define PTE_TABLE   (1UL << 1)
#define PTE_AF      (1UL << 10)
#define PTE_USER    (1UL << 6)
#define PTE_RW      (0UL << 6)
#define PTE_RO      (1UL << 7)
#define PTE_UXN     (1UL << 54)
#define PTE_PXN     (1UL << 53)
#define PTE_BLOCK   (1UL << 0)

#define PTE_ATTR_IDX(x) ((x) << 2)

#define PTE_SH_NONE     (0UL << 8)
#define PTE_SH_OUTER    (2UL << 8)
#define PTE_SH_INNER    (3UL << 8)