#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <kernel/list.h>

struct ringbuffer {
    unsigned char *buffer;
    size_t write_ptr;
    size_t read_ptr;
    size_t size;
};

struct ringbuffer *ringbuffer_create(size_t size);
void ringbuffer_destroy(struct ringbuffer *rb);
bool ringbuffer_empty(struct ringbuffer *rb);
bool ringbuffer_full(struct ringbuffer *rb);
size_t ringbuffer_read(struct ringbuffer *rb, unsigned char *buffer, size_t size);
size_t ringbuffer_write(struct ringbuffer *rb, unsigned const char *buffer, size_t size);