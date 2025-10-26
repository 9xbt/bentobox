#pragma once
#include <stddef.h>

#define SYS_read      0
#define SYS_write     1
#define SYS_seek      2
#define SYS_openat    3
#define SYS_close     4
#define SYS_fstatat   5
#define SYS_ioctl     6
#define SYS_dup       7
#define SYS_fcntl     8
#define SYS_readdir   9

#define SYS_exit      10
#define SYS_waitpid   11
#define SYS_kill      12
#define SYS_fork      13
#define SYS_exec      14
#define SYS_getpid    15
#define SYS_gettid    16
#define SYS_getppid   17
#define SYS_getpgid   18
#define SYS_setpgid   19

#define SYS_mmap      20
#define SYS_munmap    21
#define SYS_set_tls   22
#define SYS_mprotect  23

#define SYS_hostname  29
#define SYS_uname     30
#define SYS_getcwd    31
#define SYS_chdir     32
#define SYS_pipe      33
#define SYS_ppoll     34
#define SYS_sleep     35
#define SYS_gettime   36
#define SYS_faccessat 37
#define SYS_unlinkat  38
#define SYS_mkdirat   39

#define SYS_socket    40
#define SYS_bind      41
#define SYS_listen    42
#define SYS_connect   43
#define SYS_accept    44
#define SYS_recvfrom  45
#define SYS_sendto    46

long syscall_handler(size_t *args);