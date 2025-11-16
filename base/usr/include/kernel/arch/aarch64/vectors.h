#pragma once
#include <stdint.h>

void irq_register(uint8_t vector, void *handler);
void irq_unregister(uint8_t vector);
void vectors_install(void);