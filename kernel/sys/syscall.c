#include <stddef.h>
#include <stdint.h>
#include <kernel/syscall.h>
#include <kernel/printf.h>

#include <kernel/string.h>

long sys_write(int fd, void *buffer, size_t len) {
    (void)fd;
    char str[len + 1];
    memcpy(str, buffer, len);
    printf("%s", buffer);
    return 0;
}

long sys_exit(int status) {
    (void)status;
    for (;;);
    return 0;
}

syscall_func syscalls[SYSCALL_COUNT] = {
    [SYS_write]             = (syscall_func)(uintptr_t)sys_write,
    [SYS_exit]              = (syscall_func)(uintptr_t)sys_exit,
};