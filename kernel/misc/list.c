#include <kernel/malloc.h>
#include <kernel/list.h>

list_t *list_create(void) {
    list_t *list = kmalloc(sizeof(list_t));
    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
    return list;
}

void list_free(list_t *list) {
    node_t *node = list->head;
    while (node) {
        node_t *next = node->next;
        kfree(node);
        node = next;
    }
    kfree(list);
}

void list_append(list_t *list, node_t *node) {
    node->owner = list;
    if (!list->length) {
        list->head = node;
        list->tail = node;
        node->prev = NULL;
        node->next = NULL;
        list->length++;
        return;
    }
    list->tail->next = node;
    node->next = NULL;
    node->prev = list->tail;
    list->tail = node;
    list->length++;
}

node_t *list_insert(list_t *list, void *item) {
    node_t *node = kmalloc(sizeof(node_t));
    node->value = item;
    node->next  = NULL;
    node->prev  = NULL;
    node->owner = NULL;
    list_append(list, node);
    return node;
}

void list_remove(list_t *list, node_t *node) {
    if (!list || !node || node->owner != list) {
        return;
    }
    
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }
    
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }
    
    list->length--;
    node->owner = NULL;
    node->next = NULL;
    node->prev = NULL;
}

node_t *list_find(list_t *list, void *value) {
    if (!list) return NULL;
    
    foreach(node, list) {
        if (node->value == value) {
            return node;
        }
    }
    return NULL;
}

void list_remove_value(list_t *list, void *value) {
    node_t *node = list_find(list, value);
    if (node) {
        list_remove(list, node);
        kfree(node);
    }
}

void *list_pop(list_t *list) {
    if (!list || !list->head) {
        return NULL;
    }
    
    node_t *node = list->head;
    void *value = node->value;
    list_remove(list, node);
    kfree(node);
    return value;
}

void list_empty(list_t *list) {
    foreach(node, list) {
        list_remove(list, node);
    }
}
