#include <kernel/assert.h>
#include <kernel/malloc.h>
#include <kernel/list.h>

list_t *list_create(void) {
    list_t *list = kmalloc(sizeof(list_t));
    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
    list->lock = 0;
    return list;
}

node_t *list_create_node(void *value) {
    node_t *node = kmalloc(sizeof(node_t));
    node->value = value;
    node->next  = NULL;
    node->prev  = NULL;
    node->owner = NULL;
    return node;
}

void list_free(list_t *list) {
    assert(list);
    foreach(i, list) {
        kfree(i);
    }
    kfree(list);
}

void list_clear(list_t *list) {
    assert(list);
    foreach(i, list) {
        list_remove(list, i);
    }
}

node_t *list_append(list_t *list, node_t *node) {
    assert(list);
    node->owner = list;
    if (!list->length) {
        list->head = node;
        list->tail = node;
        node->prev = NULL;
        node->next = NULL;
    } else {
        list->tail->next = node;
        node->prev = list->tail;
        node->next = NULL;
        list->tail = node;
    }
    list->length++;
    return node;
}

node_t *list_insert(list_t *list, void *item) {
    return list_append(list, list_create_node(item));
}

node_t *list_find(list_t *list, void *value) {
    assert(list);
    foreach(node, list) {
        if (node->value == value)
            return node;
    }
    return NULL;
}

void list_unlink(list_t *list, node_t *node) {
    assert(list);
    assert(node);
    assert(node->owner == list);
    
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;
    
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;
    
    list->length--;
    node->owner = NULL;
    node->next = NULL;
    node->prev = NULL;
}

void list_remove(list_t *list, node_t *node) {
    list_unlink(list, node);
    kfree(node);
}

void list_remove_value(list_t *list, void *value) {
    assert(list);
    node_t *node = list_find(list, value);
    if (!node)
        return;

    list_remove(list, node);
}

void *list_pop(list_t *list) {
    assert(list);    
    node_t *node = list->head;
    if (!node)
        return NULL;

    void *value = node->value;
    list_remove(list, node);
    return value;
}