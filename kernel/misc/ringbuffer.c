#include <stdbool.h>
#include <stdint.h>
#include <kernel/ringbuffer.h>
#include <kernel/malloc.h>
#include <kernel/sched.h>
#include <kernel/list.h>

struct ringbuffer *ringbuffer_create(size_t size) {
    struct ringbuffer *rb = kmalloc(sizeof(struct ringbuffer));
    rb->buffer = kmalloc(size);
    rb->write_ptr = 0;
    rb->read_ptr = 0;
    rb->size = size;
    rb->waiting_readers = list_create();
    rb->waiting_writers = list_create();
    return rb;
};

void ringbuffer_destroy(struct ringbuffer *rb) {
    list_free(rb->waiting_readers);
    list_free(rb->waiting_writers);
    kfree(rb->buffer);
    kfree(rb);
}

bool ringbuffer_empty(struct ringbuffer *rb) {
    return rb->read_ptr == rb->write_ptr;
}

bool ringbuffer_full(struct ringbuffer *rb) {
    return (rb->write_ptr + 1) % rb->size == rb->read_ptr;
}

size_t ringbuffer_read(struct ringbuffer *rb, unsigned char *buffer, size_t size) {
    unsigned char *buf = (unsigned char *)buffer;
    size_t i = 0;
    
    while (i < size && rb->read_ptr != rb->write_ptr) {
        buf[i] = rb->buffer[rb->read_ptr];
        rb->read_ptr = (rb->read_ptr + 1) % rb->size;
        i++;
    }

    foreach(node, rb->waiting_writers) {
        sched_unblock(node->value);
        list_remove(rb->waiting_writers, node);
    }
    return i;
}

size_t ringbuffer_write(struct ringbuffer *rb, unsigned char *buffer, size_t size) {
    unsigned char *buf = (unsigned char *)buffer;
    size_t i = 0;
    
    while (i < size && (rb->write_ptr + 1) % rb->size != rb->read_ptr) {
        rb->buffer[rb->write_ptr] = buf[i];
        rb->write_ptr = (rb->write_ptr + 1) % rb->size;
        i++;
    }

    foreach(node, rb->waiting_readers) {
        sched_unblock(node->value);
        list_remove(rb->waiting_readers, node);
    }
    return i;
}