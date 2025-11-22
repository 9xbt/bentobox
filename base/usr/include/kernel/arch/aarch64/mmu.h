#pragma once

#define PTE_ADDR_MASK 0x0000fffffffff000
#define PTE_GET_ADDR(x) ((x) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(x) ((x) & ~PTE_ADDR_MASK)

#define PTE_VALID   (1UL << 0)
#define PTE_TABLE   (1UL << 1)
#define PTE_4K_PAGE (1UL << 1)
#define PTE_AF      (1UL << 10)
#define PTE_RW      (0UL << 6)
#define PTE_RO      (1UL << 6)
#define PTE_USER_RW (1UL << 6)
#define PTE_USER_RO (3UL << 6)
#define PTE_UXN     (1UL << 54)
#define PTE_PXN     (1UL << 53)
#define PTE_BLOCK   (0UL << 1)
#define PTE_COW     (1UL << 55)

#define PTE_WRITABLE    PTE_USER_RW