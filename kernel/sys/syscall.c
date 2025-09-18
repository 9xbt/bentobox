#include <stddef.h>
#include <stdint.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/syscall.h>
#include <kernel/printf.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/mmu.h>

static long sys_read_write(int fd, void *buffer, size_t len, bool write) {
    struct file *file = file_get(fd);
    if (!file || !file->open)
        return -EBADFD;
    if (!file->node)
        return -ENOENT;
    if (!file->node->ops || (!file->node->ops->write && write) || (!file->node->ops->read && !write))
        return 0;

    long ret = write ?
        vfs_write(file->node, buffer, file->offset, len) :
        vfs_read(file->node, buffer, file->offset, len);
    file->offset += ret;
    return ret;
}

long sys_read(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, false);
}

long sys_write(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, true);
}

long sys_exit(int status) {
    (void)status;
    for (;;);
    return 0;
}

#define MAP_ANON  0x1000
#define MAP_FIXED   0x10
#define MAP_PRIVATE 0x02
#define MAP_SHARED  0x01

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

long sys_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr;
    if (!length)
        return -EINVAL;
    
    if (fd == -1) {
        if (offset != 0)
            return -EINVAL;
        if (flags & MAP_FIXED) {
            dprintf(LOG_ERR, "\033[93mmmap:\033[0m fixed mappings aren't implemented!\n");
            return -ENOSYS;
        }

        uint64_t vma_flags = 0;
        if (prot != PROT_NONE) {
            vma_flags = PTE_USER;
            if (prot & PROT_READ) vma_flags |= PTE_PRESENT;
            if (prot & PROT_WRITE) vma_flags |= PTE_WRITABLE;
        }

        size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
        void *ptr = vmalloc(this->parent->vma, this->parent->pm, pages, vma_flags);

        return (long)ptr;
    }
    return -ENOSYS;
}

long sys_set_tls(uint64_t fs) {
    write_fs(fs);
    this->ctx.fs = fs;
    return 0;
}

typedef long (*syscall_func)(long, long, long, long, long, long);

syscall_func syscalls[] = {
    [SYS_exit]      = (syscall_func)(uintptr_t)sys_exit,
    [SYS_read]      = (syscall_func)(uintptr_t)sys_read,
    [SYS_write]     = (syscall_func)(uintptr_t)sys_write,
    [SYS_mmap]      = (syscall_func)(uintptr_t)sys_mmap,
    [SYS_set_tls]   = (syscall_func)(uintptr_t)sys_set_tls
};

long syscall_handler(size_t *args) {
    if (args[0] >= sizeof syscalls / sizeof(void *) || !syscalls[args[0]]) {
        dprintf(LOG_INFO, "\033[93muser:\033[0m unknown syscall %lu\n", args[0]);
        return -ENOSYS;
    }

    syscall_func handler = syscalls[args[0]];
    return handler(args[1], args[2], args[3], args[4], args[5], args[6]);
}