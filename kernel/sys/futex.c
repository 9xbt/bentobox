#include <kernel/malloc.h>
#include <kernel/futex.h>
#include <kernel/errno.h>
#include <kernel/list.h>
#include <kernel/time.h>

list_t *futex_waiters = NULL;
spinlock_t futex_lock = 0;

void futex_initialize(void) {
    futex_waiters = list_create();
}

long futex_wait(int *pointer, int expected, const struct timespec *time) {
    acquire(&futex_lock);
    
    int pointer_value;
    if (copy_from_user(&pointer_value, pointer, sizeof(int)) < 0) {
        release(&futex_lock);
        return -EFAULT;
    }

    if (pointer_value != expected) {
        release(&futex_lock);
        return -EAGAIN;
    }
    
    struct futex_waiter *waiter = kmalloc(sizeof(struct futex_waiter));
    waiter->address = pointer;
    waiter->thread = this;
    list_insert(futex_waiters, waiter);
    
    release(&futex_lock);
    
    if (time) {
        sched_sleep(time->tv_sec * 1000000000ULL + time->tv_nsec);
    } else {
        this->state = THREAD_PAUSED;
        sched_yield();
    }

    acquire(&futex_lock);
    
    foreach_safe(node, futex_waiters) {
        struct futex_waiter *w = node->value;
        if (w == waiter) {
            list_remove(futex_waiters, node);
            kfree(waiter);
            break;
        }
    }
    
    release(&futex_lock);
    return 0;
}

long futex_wake(int *pointer) {
    acquire(&futex_lock);

    foreach_safe(node, futex_waiters) {
        struct futex_waiter *waiter = node->value;
        if (waiter->address == pointer) {
            waiter->thread->state = THREAD_RUNNING;
            list_remove(futex_waiters, node);
            kfree(waiter);
        }
    }
    
    release(&futex_lock);
    return 0;
}