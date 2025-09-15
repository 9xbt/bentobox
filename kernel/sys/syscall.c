#include <stddef.h>
#include <stdint.h>
#include <kernel/syscall.h>
#include <kernel/printf.h>
#include <kernel/errno.h>

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

typedef long (*syscall_func)(long, long, long, long, long, long);

syscall_func syscalls[] = {
    [SYS_write]             = (syscall_func)(uintptr_t)sys_write,
    [SYS_exit]              = (syscall_func)(uintptr_t)sys_exit,
};

void syscall_handler(size_t *args) {
    if (args[0] >= sizeof syscalls / sizeof(void *) || !syscalls[args[0]]) {
        dprintf(LOG_INFO, "\033[93muser:\033[0m unknown syscall %lu\n", args[0]);
        args[0] = -ENOSYS;
        return;
    }

    syscall_func handler = syscalls[args[0]];
    args[0] = handler(args[1], args[2], args[3], args[4], args[5], args[6]);
}