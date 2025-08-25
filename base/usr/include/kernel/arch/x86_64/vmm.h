#pragma once

#define PTE_ADDR_MASK 0x000ffffffffff000
#define PTE_GET_ADDR(x) ((x) & PTE_ADDR_MASK)
#define PTE_GET_FLAGS(x) ((x) & ~PTE_ADDR_MASK)

#define PTE_PRESENT  1ul
#define PTE_WRITABLE 2ul
#define PTE_USER     4ul
#define PTE_WT       8ul
#define PTE_CD       16ul
#define PTE_HUGE     128ul
#define PTE_NX (1ul << 63)