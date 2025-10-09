#include <kernel/spinlock.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/fifo.h>

fifo_t *__fifo_create(long size, long object_size) {
    fifo_t *fifo = kmalloc(sizeof(fifo_t));
    fifo->data = kmalloc(sizeof(int) * size);
    fifo->size = size;
    fifo->head = 0;
    fifo->tail = 0;
    fifo->count = 0;
    fifo->object_size = object_size;
    fifo->lock = 0;
    return fifo;
}

long fifo_is_full(fifo_t *fifo) {
    return fifo->count == fifo->size;
}

long fifo_is_empty(fifo_t *fifo) {
    return fifo->count == 0;
}

long __fifo_enqueue(fifo_t *fifo, const void *value) {
    if (fifo_is_full(fifo))
        return -1;

    acquire(&(fifo->lock));

    memcpy((char *)fifo->data + (fifo->tail * fifo->object_size), value, fifo->object_size);
    fifo->tail = (fifo->tail + 1) % fifo->size;
    fifo->count++;

    release(&(fifo->lock));
    return fifo->object_size;
}

long __fifo_dequeue(fifo_t *fifo, void *value) {
    if (fifo_is_empty(fifo))
        return -1;
    
    acquire(&fifo->lock);

    memcpy(value, (char *)fifo->data + (fifo->head * fifo->object_size), fifo->object_size);
    fifo->head = (fifo->head + 1) % fifo->size;
    fifo->count--;
    
    release(&fifo->lock);
    return fifo->object_size;
}

void fifo_destroy(fifo_t *fifo) {
    if (fifo) {
        kfree(fifo->data);
        kfree(fifo);
    }
}