#include <kernel/spinlock.h>

void acquire(spinlock_t *lock) {
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
#ifdef __x86_64__ 
        __builtin_ia32_pause();
#endif
    }
}

void release(spinlock_t *lock) {
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

bool try_acquire(spinlock_t *lock) {
    return !__atomic_test_and_set(lock, __ATOMIC_ACQUIRE);
}