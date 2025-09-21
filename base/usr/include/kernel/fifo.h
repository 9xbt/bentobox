#pragma once
#include <kernel/spinlock.h>

struct fifo {
    void *data;
    long head;
    long tail;
    long count;
    long size;
    long object_size;
    spinlock_t lock;
};

struct fifo *__fifo_create(long size, long object_size);
long fifo_is_full(struct fifo *fifo);
long fifo_is_empty(struct fifo *fifo);
long __fifo_enqueue(struct fifo *fifo, const void *value);
long __fifo_dequeue(struct fifo *fifo, void *value);
void fifo_destroy(struct fifo *fifo);

#define fifo_create(size, type) __fifo_create(size, sizeof(type))

#define fifo_enqueue(fifo, value) ({ \
    __typeof__(value) _temp = (value); \
    __fifo_enqueue(fifo, &_temp); \
})

#define fifo_dequeue(fifo, value_ptr) __fifo_dequeue(fifo, value_ptr)

#define fifo_dequeue_value(fifo, type) ({ \
    type _result; \
    int _ret = __fifo_dequeue(fifo, &_result); \
    _ret == 0 ? _result : (type){0}; \
})
