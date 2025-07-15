#pragma once
#define IOAPIC_ID     0x0
#define IOAPIC_VER    0x1
#define IOAPIC_ARB    0x2
#define IOAPIC_REDTBL 0x10

#define IOAPIC_REGSEL 0x0
#define IOAPIC_IOWIN  0x10

#include <stdint.h>
#include <stdbool.h>

void ioapic_install(void);
void ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool mask);