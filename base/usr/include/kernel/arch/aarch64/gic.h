#pragma once

#define GICD_CTLR       0x000
#define GICD_ISENABLER0 0x100
#define GICD_IPRIORITYR 0x400
#define GICD_SGIR       0xF00

#define GICC_CTLR       0x00
#define GICC_PMR        0x04
#define GICC_IAR        0x0C
#define GICC_EOIR       0x10

void gicc_install(void);
void gic_install(void);