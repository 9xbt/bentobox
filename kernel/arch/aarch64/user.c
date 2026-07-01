#include <stdint.h>
#include <kernel/string.h>
#include <kernel/malloc.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/mmu.h>

static long __user_copy(void *restrict dest, const void *restrict src, size_t n) {
    this->user_copy_status = 0;
    this->doing_user_copy = true;
    memcpy(dest, src, n);
    this->doing_user_copy = false;
    return this->user_copy_status;
}

long check_user_address(const void *addr) {
    if (!addr || (uintptr_t)addr >= hhdm_offset || !mmu_get_physical(this_proc->pm, (void *)ALIGN_DOWN((uintptr_t)addr, PAGE_SIZE)))
        return -EFAULT;
    return 0;
}

long copy_from_user(void *restrict dest, const void *restrict src, size_t n) {
    if (check_user_address(src) < 0)
        return -EFAULT;
    return __user_copy(dest, src, n);
}

long copy_to_user(void *restrict dest, const void *restrict src, size_t n) {
    if (check_user_address(dest) < 0)
        return -EFAULT;
    return __user_copy(dest, src, n);
}

long strnlen_user(const char *s, size_t maxlen) {
    this->user_copy_status = 0;
    this->doing_user_copy = true;
    size_t len = strnlen(s, maxlen);
    this->doing_user_copy = false;

    if (this->user_copy_status != 0)
        return this->user_copy_status;
    return len;
}

long __user_string_copy(const char *s, size_t maxlen, char **out) {
    long len = strnlen_user(s, maxlen);
    if (len < 0)
        return len;
    if ((size_t)len > maxlen)
        return -ENAMETOOLONG;
    
    char *str = kmalloc(len + 1);
    if (copy_from_user(str, s, len) < 0) {
        kfree(str);
        return -EFAULT;
    }
    str[len] = 0;
    *out = str;
    return 0;
}