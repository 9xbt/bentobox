#pragma once
#include <stddef.h>

#define SYS_read    0
#define SYS_write   1
#define SYS_seek    2
#define SYS_open    3
#define SYS_close   4
#define SYS_fstatat 5
#define SYS_ioctl   6

#define SYS_exit    10
#define SYS_wait    11
#define SYS_kill    12
#define SYS_fork    13
#define SYS_exec    14
#define SYS_getpid  15
#define SYS_gettid  16
#define SYS_getppid 17

#define SYS_mmap    20
#define SYS_munmap  21
#define SYS_set_tls 22

long syscall_handler(size_t *args);