#pragma once
#include <stdint.h>
#include <kernel/acpi.h>

#define GICD_CTLR       0x000
#define GICD_ISENABLER0 0x100
#define GICD_IPRIORITYR 0x400
#define GICD_SGIR       0xF00

#define GICC_CTLR       0x00
#define GICC_PMR        0x04
#define GICC_IAR        0x0C
#define GICC_EOIR       0x10

void gic_install(void);
void gic_send_sgi(uint8_t sgiid, uint8_t mask);
void gicd_write(struct madt_gicd *gicd, uint32_t offset, uint32_t value);
void gicc_write(struct madt_gicc *gicc, uint32_t offset, uint32_t value);
uint32_t gicd_read(struct madt_gicd *gicd, uint32_t offset);
uint32_t gicc_read(struct madt_gicc *gicc, uint32_t offset);