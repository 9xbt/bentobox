#pragma once
#include <stddef.h>
#include <kernel/spinlock.h>

typedef struct node {
    struct node *prev;
    struct node *next;
    void *value;
    struct list *owner;
} node_t;

typedef struct list {
    node_t *head;
    node_t *tail;
    size_t length;
    spinlock_t lock;
} list_t;

list_t *list_create(void);
void    list_free(list_t *list);
void    list_append(list_t *list, node_t *node);
node_t *list_insert(list_t *list, void *item);
void    list_remove(list_t *list, node_t *node);
node_t *list_find(list_t *list, void *value);
void    list_remove_value(list_t *list, void *value);
void   *list_pop(list_t *list);
void    list_clear(list_t *list);

#define foreach(__i, list) for (node_t *__i = (list)->head; __i; __i = __i->next)
#define foreach_safe(__i, list) for (node_t *__i = (list)->head, *__next_##__i = __i ? __i->next : NULL; __i; __i = __next_##__i, __next_##__i = __i ? __i->next : NULL)