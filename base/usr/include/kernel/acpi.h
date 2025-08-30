#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
} __attribute__((packed));

struct acpi_xsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;

    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_rsdt {
    struct acpi_sdt sdt;
    char table[];
} __attribute__((packed));

struct acpi_xsdt {
    struct acpi_sdt sdt;
    char table[];
} __attribute__((packed));

struct acpi_gas {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct acpi_madt {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;

    uint32_t lapic_addr;
    uint32_t flags;

    char table[];
} __attribute__((packed));

struct acpi_hpet {
    struct acpi_sdt sdt;
    uint32_t event_timer_block_id;
    uint32_t reserved;
    uint64_t address;
    uint8_t id;
    uint16_t min_ticks;
    uint8_t page_protection;
}__attribute__((packed));

struct madt_entry {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct madt_lapic {
    struct madt_entry entry;
    uint8_t core_id;
    uint8_t id;
    uint32_t flags;
} __attribute__((packed));

struct madt_ioapic {
    struct madt_entry entry;
    uint8_t id;
    uint8_t resv;
    uint32_t address;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_iso {
    struct madt_entry entry;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct madt_gicc {
    struct madt_entry entry;
    uint16_t reserved;
    uint32_t cpu_interface;
    uint32_t uid;
    uint32_t flags;
    uint32_t parking_protocol_ver;
    uint32_t performance_gsiv;
    uint64_t parked_address;
    uint64_t phys_base;
    uint64_t gigv;
    uint64_t gigh;
    uint32_t vgiv_maintenance_int;
    uint64_t gigr_base_addr;
    uint64_t mpidr;
    uint8_t  efficiency_class;
    uint8_t  reserved_2;
    uint16_t spe_overflow_int;
    uint16_t trbe_int;
} __attribute__((packed));

struct madt_gicd {
    struct madt_entry entry;
    uint16_t reserved;
    uint32_t gic_id;
    uint64_t phys_base;
    uint32_t reserved_2;
    uint8_t  version;
    char     reserved_3[3];
} __attribute__((packed));

extern struct acpi_madt   *madt;
extern struct madt_lapic  **madt_lapic_list;
extern struct madt_ioapic **madt_ioapic_list;
extern struct madt_iso    **madt_iso_list;
extern size_t madt_lapics;
extern size_t madt_ioapics;
extern size_t madt_isos;

extern struct madt_gicc **madt_gicc_list;
extern struct madt_gicd **madt_gicd_list;
extern size_t madt_giccs;
extern size_t madt_gicds;

void  acpi_install(void);
void  acpi_reboot(void);
void *acpi_find_table(const char *signature);
void  madt_init(void);