#pragma once
#include <stddef.h>

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
} list_t;

list_t *list_create(void);
void    list_free(list_t *list);
void    list_append(list_t *list, node_t *node);
node_t *list_insert(list_t *list, void *item);
void    list_remove(list_t *list, node_t *node);
node_t *list_find(list_t *list, void *value);
void    list_remove_value(list_t *list, void *value);
void   *list_pop(list_t *list);
void    list_empty(list_t *list);

#define foreach(i, list) for (node_t *i = (list)->head; i; i = i->next)