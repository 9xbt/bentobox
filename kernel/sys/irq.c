#include <kernel/assert.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/irq.h>
#include <kernel/smp.h>

irq_t **irq_handlers = NULL;

static irq_domain_t *get_root_domain(irq_t *irq) {
    irq_domain_t *domain = irq->domain;
    while (domain->parent)
        domain = domain->parent;
    return domain;
}

int irq_register(irq_t *irq, int hint) {
    if (hint != -1) {
        assert(!irq_handlers[hint]);
        irq_handlers[hint] = irq;
        return hint;
    }
    
    irq_domain_t *domain = get_root_domain(irq);
    for (int i = domain->base; i < domain->count; i++) {
        if (!irq_handlers[i]) {
            irq_handlers[i] = irq;
            return i;
        }
    }
    panic("Out of interrupts!");
    return -1;
}

irq_t *irq_allocate(irq_domain_t *domain, void *handler, int hwirq, int hint) {
    assert(domain);
    irq_t *irq = kmalloc(sizeof(irq_t));
    irq->domain = domain;
    irq->virq = irq_register(irq, hint);
    irq->hwirq = hwirq;
    irq->flags = 0;
    irq->handler = handler;
    if (domain->alloc)
        domain->alloc(domain, irq->virq, irq->hwirq);
    return irq;
}

irq_chip_t *irq_create_chip(void (*eoi)(struct irq *)) {
    irq_chip_t *chip = kmalloc(sizeof(irq_chip_t));
    chip->eoi = eoi;
    return chip;
}

irq_domain_t *irq_create_domain(irq_chip_t *chip, irq_domain_t *parent,
    int base, int count, void(*alloc)(struct irq_domain *, int, int),
    void(*free)(struct irq_domain *, int, int))
{
    irq_domain_t *domain = kmalloc(sizeof(irq_domain_t));
    domain->chip   = chip;
    domain->parent = parent;
    domain->base   = base;
    domain->count  = count;
    domain->alloc  = alloc;
    domain->free   = free;
    domain->translate = NULL;
    return domain;
}

void irq_eoi(irq_t *irq) {
    irq_domain_t *domain = get_root_domain(irq);
    if (domain->chip && domain->chip->eoi)
        domain->chip->eoi(irq);
}

void irq_allocate_table(uint32_t num) {
    irq_handlers = kmalloc(sizeof(irq_t *) * num);
    memset(irq_handlers, 0, sizeof(irq_t *) * num);
}