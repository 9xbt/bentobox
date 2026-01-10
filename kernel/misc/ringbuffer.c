#include <stdbool.h>
#include <kernel/ringbuffer.h>
#include <kernel/string.h>
#include <kernel/malloc.h>
#include <kernel/sched.h>
#include <kernel/list.h>

struct ringbuffer *ringbuffer_create(size_t size) {
    struct ringbuffer *rb = kmalloc(sizeof(struct ringbuffer));
    rb->buffer = kmalloc(size);
    rb->read_ptr = 0;
    rb->write_ptr = 0;
    rb->size = size;
    rb->lock = 0;
    return rb;
};

struct ringbuffer *ringbuffer_clone(struct ringbuffer *rb) {
    struct ringbuffer *clone = ringbuffer_create(rb->size);
    memcpy(clone->buffer, rb->buffer, rb->size);
    clone->read_ptr = rb->read_ptr;
    clone->write_ptr = rb->write_ptr;
    return clone;
}

void ringbuffer_destroy(struct ringbuffer *rb) {
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
    acquire(&rb->lock);
    size_t i = 0;
    while (i < size && rb->read_ptr != rb->write_ptr) {
        buffer[i] = rb->buffer[rb->read_ptr];
        rb->read_ptr = (rb->read_ptr + 1) % rb->size;
        i++;
    }
    release(&rb->lock);
    return i;
}

size_t ringbuffer_write(struct ringbuffer *rb, unsigned const char *buffer, size_t size) {
    acquire(&rb->lock);
    size_t i = 0;
    while (i < size && (rb->write_ptr + 1) % rb->size != rb->read_ptr) {
        rb->buffer[rb->write_ptr] = buffer[i];
        rb->write_ptr = (rb->write_ptr + 1) % rb->size;
        i++;
    }
    release(&rb->lock);
    return i;
}

size_t ringbuffer_peek(struct ringbuffer *rb, unsigned char *buffer, size_t size, size_t offset) {
    acquire(&rb->lock);
    size_t i = 0;
    size_t read_ptr = rb->read_ptr + offset;
    while (i < size && read_ptr != rb->write_ptr) {
        buffer[i] = rb->buffer[read_ptr];
        read_ptr = (read_ptr + 1) % rb->size;
        i++;
    }
    release(&rb->lock);
    return i;
}