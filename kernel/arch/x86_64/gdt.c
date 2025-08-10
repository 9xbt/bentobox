#include <stdbool.h>
#include <stdint.h>
#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/printf.h>

struct gdt_table gdt_table;
struct gdtr gdt_descriptor;

void gdt_set_entry(uint8_t index, uint16_t limit, uint64_t base, uint8_t access, uint8_t gran) {
    if (index > 4) {
        int i = index - 5;
        gdt_table.tss_entries[i].limit = limit;
        gdt_table.tss_entries[i].base_low = base & 0xFFFF;
        gdt_table.tss_entries[i].base_mid = (base >> 16) & 0xFF;
        gdt_table.tss_entries[i].access = access;
        gdt_table.tss_entries[i].gran = gran;
        gdt_table.tss_entries[i].base_high = (base >> 24) & 0xFF;
        gdt_table.tss_entries[i].base_long = (base >> 32);
        return;
    }

    gdt_table.gdt_entries[index].limit = limit;
    gdt_table.gdt_entries[index].base_low = base & 0xFFFF;
    gdt_table.gdt_entries[index].base_mid = (base >> 16) & 0xFF;
    gdt_table.gdt_entries[index].access = access;
    gdt_table.gdt_entries[index].gran = gran;
    gdt_table.gdt_entries[index].base_high = (base >> 24) & 0xFF;
}

void gdt_flush(void) {
    asm volatile (
        "lgdt %0\n"
		"mov $0x10, %%ax\n"
		"mov %%ax, %%ds\n"
		"mov %%ax, %%es\n"
		"mov %%ax, %%ss\n"
        : : "m"(gdt_descriptor) : "rax", "memory"
    );
}

void gdt_install(void) {
    gdt_set_entry(0, 0x0000, 0x00000000, 0x00, 0x00);
    gdt_set_entry(1, 0x0000, 0x00000000, 0x9A, 0x20);
    gdt_set_entry(2, 0x0000, 0x00000000, 0x92, 0x00);
    gdt_set_entry(3, 0x0000, 0x00000000, 0xF2, 0x00);
    gdt_set_entry(4, 0x0000, 0x00000000, 0xFA, 0x20);

    gdt_descriptor = (struct gdtr) {
        .size = sizeof(gdt_table) - 1,
        .offset = (uint64_t)&gdt_table
    };
    gdt_flush();

    dprintf(6, "%s:%d: GDT address: 0x%p\n", __FILE__, __LINE__, (uint64_t)&gdt_descriptor);
}