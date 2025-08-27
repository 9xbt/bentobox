#pragma once
#define IOAPIC_ID     0x0
#define IOAPIC_VER    0x1
#define IOAPIC_ARB    0x2
#define IOAPIC_REDTBL 0x10

#define IOAPIC_REGSEL 0x0
#define IOAPIC_IOWIN  0x10

#define IOAPIC_DELIVERY_MODE_FIXED      0x0
#define IOAPIC_DELIVERY_MODE_LOWPRIO    0x100
#define IOAPIC_DELIVERY_MODE_SMI        0x200
#define IOAPIC_DELIVERY_MODE_NMI        0x400
#define IOAPIC_DELIVERY_MODE_INIT       0x500
#define IOAPIC_DELIVERY_MODE_EXTINT     0x700

#define IOAPIC_DEST_MODE_PHYSICAL   0x0
#define IOAPIC_DEST_MODE_LOGICAL    0x800

#define IOAPIC_INT_MASK             0x10000

#define IOAPIC_DEST_FIELD_SHIFT     56

#include <stdbool.h>
#include <stdint.h>

void ioapic_install(void);
void ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool mask);