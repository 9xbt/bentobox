#include <stdatomic.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/spinlock.h>
#include <kernel/malloc.h>
#include <kernel/mutex.h>
#include <kernel/sched.h>

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = NULL;
    m->queue = NULL;
}

void mutex_lock(mutex_t *m) {
    if (!this) return;

    for (;;) {
        acquire(&m->lock);

        if (!m->locked) {
            m->locked = 1;
            m->owner = this;
            release(&m->lock);
            return;
        }

        mutex_list_t *i = m->queue;
        int already_queued = 0;
        while (i) {
            if (i->proc == this) {
                already_queued = 1;
                break;
            }
            i = i->next;
        }

        if (!already_queued) {
            mutex_list_t *node = kmalloc(sizeof(mutex_list_t));
            node->proc = this;
            node->next = NULL;

            if (!m->queue) {
                m->queue = node;
            } else {
                i = m->queue;
                while (i->next) i = i->next;
                i->next = node;
            }
        }

        release(&m->lock);
        sched_block(TASK_PAUSED);
    }
}

void mutex_unlock(mutex_t *m) {
    acquire(&m->lock);

    if (!this || m->owner != this) {
        release(&m->lock);
        return;
    }

    m->locked = 0;
    m->owner = NULL;

    if (m->queue != NULL) {
        mutex_list_t *first_thread = m->queue;
        m->queue = first_thread->next;
        sched_unblock(first_thread->proc); 
        kfree(first_thread);
    }

    release(&m->lock);
}