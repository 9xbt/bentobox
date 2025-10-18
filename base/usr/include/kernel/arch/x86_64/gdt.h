#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <kernel/smp.h>

struct gdt_entry {
    uint16_t limit;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_entry_long {
    uint16_t limit;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
    uint32_t base_long;
    uint32_t resv;
} __attribute__((packed));

struct gdt_table {
    struct gdt_entry gdt_entries[5];
    struct gdt_entry_long tss_entries[SMP_MAX_CORES];
} __attribute__((packed));

struct gdtr {
    uint16_t size;
    uint64_t offset;
} __attribute__((packed));

extern struct gdt_table gdt_table;

void gdt_install(void);
void gdt_set_entry(uint16_t index, uint16_t limit, uint64_t base, uint8_t access, uint8_t gran);
void gdt_flush(void);