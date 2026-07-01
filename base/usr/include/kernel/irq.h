#pragma once
#include <stdint.h>

typedef struct irq {
    struct irq_domain *domain;
    int virq;
    int hwirq;
    void *handler;
    uint32_t flags;
} irq_t;

typedef struct irq_chip {
    void (*eoi)(struct irq *irq);
} irq_chip_t;

typedef struct irq_domain {
    struct irq_chip   *chip;
    struct irq_domain *parent;
    int base;
    int count;
    void(*alloc)(struct irq_domain *domain, int virq, int hwirq);
    void(*free)(struct irq_domain *domain, int virq, int hwirq);
    int (*translate)(struct irq_domain *domain, ...);
} irq_domain_t;

irq_t *irq_allocate(irq_domain_t *domain, void *handler, int hwirq, int hint);
irq_chip_t *irq_create_chip(void (*eoi)(struct irq *));
irq_domain_t *irq_create_domain(irq_chip_t *chip, irq_domain_t *parent,
    int base, int count, void(*alloc)(struct irq_domain *, int, int),
    void(*free)(struct irq_domain *, int, int));
void irq_eoi(irq_t *irq);
void irq_allocate_table(uint32_t num);