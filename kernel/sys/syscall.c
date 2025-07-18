#include <linux/resource.h>
#include <sys/utsname.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/tsc.h>
#include <kernel/unixpipe.h>
#include <kernel/syscall.h>
#include <kernel/version.h>
#include <kernel/assert.h>
#include <kernel/printf.h>
#include <kernel/signal.h>
#include <kernel/string.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/video.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/fd.h>

long sys_read_write(int fd_num, void *buffer, size_t len, bool write) {
    struct fd *fd = fd_get(fd_num);
    if (!fd || !fd->open)
        return -EBADFD;
    if (!fd->node)
        return -ENOENT;
    if ((!fd->node->write && write) || (!fd->node->read && !write))
        return 0;

    // TODO: fix this by adding proper polling
    // if (!(fd->flags & O_NONBLOCK)) {
    //     vfs_poll(fd->node);
    // }

    long ret = write ?
        vfs_write(fd->node, buffer, fd->offset, len) :
        vfs_read(fd->node, buffer, fd->offset, len);
    fd->offset += ret;
    return ret;
    return 0;
}

long sys_read(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, false);
}

long sys_write(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, true);
}

long sys_open(const char *pathname, int flags, mode_t mode) {
    (void)mode;
    return fd_open(pathname, flags);
}

long sys_close(int fd) {
    return fd_close(fd);
}

long sys_stat(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf)
        return -EFAULT;
    return vfs_stat(vfs_open(this->cwd, pathname, false, false), statbuf, false);
}

long sys_fstat(int fd_num, struct stat *statbuf) {
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    if (!statbuf)
        return -EFAULT;
    return vfs_stat(fd->node, statbuf, false);
}

long sys_lstat(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf)
        return -EFAULT;
    return vfs_stat(vfs_open(this->cwd, pathname, false, false), statbuf, true);
}

long sys_newfstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    (void)flags;
    if (!pathname || !statbuf)
        return -EFAULT;
    
    struct vfs_node *node = NULL;
    if (pathname[0] == '/') {
        node = vfs_open(this->cwd, pathname, false, false);
    } else if (dirfd == AT_FDCWD) {
        node = vfs_open(this->cwd, pathname, false, false);
    } else {
        if (dirfd < 0 || dirfd >= (signed)(sizeof this->fd_table / sizeof(struct fd)) || !this->fd_table[dirfd].node) {
            return -EBADF;
        }

        struct fd *dir_fd = fd_get(dirfd);
        if (!dir_fd)
            return -EBADF;
        if (dir_fd->node->type != VFS_DIRECTORY)
            return -ENOTDIR;

        struct vfs_node *child = dir_fd->node->children;
        while (child) {
            if (!strcmp(child->name, pathname)) {
                node = child;
                break;
            }
            child = child->next;
        }
    }
    return vfs_stat(node, statbuf, true);
}

long sys_lseek(int fd_num, off_t offset, int whence) {
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    if (fd->node->type == VFS_CHARDEVICE)
        return -ESPIPE;

    switch (whence) {
        case SEEK_SET:
            fd->offset = offset;
            break;
        case SEEK_CUR:
            fd->offset += offset;
            break;
        case SEEK_END:
            fd->offset = fd->node->size + offset;
            break;
    }

    return fd->offset;
}

long sys_mmap(void *addr, size_t length, int prot, int flags, int fd_num, off_t offset) {
    if (length == 0)
        return -EINVAL;

    if (flags & MAP_ANONYMOUS) {
        if (offset != 0 || fd_num != -1) return -EINVAL;

        uint64_t vma_flags = 0;
        if (prot != PROT_NONE) {
            vma_flags = PTE_USER;
            if (prot & PROT_READ) vma_flags |= PTE_PRESENT;
            if (prot & PROT_WRITE) vma_flags |= PTE_WRITABLE;
        }

        size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
        void *ptr = (flags & MAP_FIXED)
            ? vma_map(this->vma, pages, 0, (uint64_t)addr, vma_flags)
            : vma_map(this->vma, pages, 0, 0, vma_flags);

        if (!ptr) return -ENOMEM;

        if (prot != PROT_NONE) memset(ptr, 0, pages * PAGE_SIZE);

        return (long)ptr;
    }

    struct fd *fd = fd_get(fd_num);
    if (!fd || !fd->node->mmap)
        return -ENODEV;
    return fd->node->mmap(addr, length, prot, flags, fd_num, offset);
}

long sys_munmap(void *addr, size_t length) {
    if (addr == NULL ||
        (uintptr_t)addr % PAGE_SIZE != 0 ||
        length == 0)
        return -EINVAL;

    sched_lock();
    size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
    uintptr_t start_addr = (uintptr_t)addr;
    uintptr_t end_addr = start_addr + (pages * PAGE_SIZE);

    uintptr_t current_addr = start_addr;

    while (current_addr < end_addr) {
        vma_unmap_addr(this->vma, (void *)current_addr);
        current_addr += PAGE_SIZE;
    }

    sched_unlock();
    return 0;
}

long sys_brk(void *addr) {
    uintptr_t current_brk = this->brk;
    
    uintptr_t new_brk = (uintptr_t)addr;
    if (!new_brk || new_brk < current_brk || new_brk == current_brk)
        return current_brk;
    
    if (new_brk > current_brk) {
        uintptr_t map_start = ALIGN_UP(current_brk, PAGE_SIZE);
        uintptr_t map_end = ALIGN_UP(new_brk, PAGE_SIZE);
        size_t length = map_end - map_start;
        
        if (length > 0) {
            size_t pages = length / PAGE_SIZE;
            vma_map(this->vma, pages, 0, map_start, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        }
        this->brk = map_end;
    } else {
        dprintf("%s:%d: %s: TODO: shrinking\n", __FILE__, __LINE__, __func__);
    }
    return new_brk;
}

long sys_rt_sigaction() {
    return 0;
}

long sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset, size_t sigsetsize) {
#if 0
    if (!set)
        return -EFAULT;
    switch (how) {
        case SIG_BLOCK:
            this->signal_mask |= set->__val[0];
            break;
        case SIG_UNBLOCK:
            this->signal_mask &= set->__val[0];
            break;
        case SIG_SETMASK:
            this->signal_mask = set->__val[0];
            break;
        default:
            return -EINVAL;
    }
#endif
    return 0;
}

long sys_ioctl(int fd_num, int op, void *arg) {
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    if (!fd->node->isatty)
        return -ENOTTY;
    if (!arg)
        return -EFAULT;
    return fd->node->tty_ops.ioctl(fd_num, op, arg);
}

struct iovec {
    void *iov_base;
    size_t iov_len;
};

#define IOV_MAX 1024

long sys_read_writev(int fd_num, const struct iovec *iov, int iovcnt, bool write) {
    if (iovcnt < 0 || iovcnt > IOV_MAX)
        return -EINVAL;
    if (!iov && iovcnt > 0)
        return -EFAULT;
    
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    if ((!fd->node->write && write) || (!fd->node->read && !write))
        return -EINVAL;
    
    ssize_t total_written = 0;
    
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base && iov[i].iov_len > 0)
            return -EFAULT;
        if (iov[i].iov_len == 0)
            continue;
        
        long ret = write ?
            fd->node->write(fd->node, iov[i].iov_base, fd->offset, iov[i].iov_len) :
            fd->node->read(fd->node, iov[i].iov_base, fd->offset, iov[i].iov_len);
        if (ret < 0) {
            if (total_written == 0)
                return ret;
            break;
        }
        
        fd->offset += ret;
        total_written += ret;
        
        if ((size_t)ret < iov[i].iov_len)
            break;
    }
    return total_written;
}

long sys_writev(int fd, const struct iovec *iov, int iovcnt) {
    return sys_read_writev(fd, iov, iovcnt, true);
}

long sys_readv(int fd, const struct iovec *iov, int iovcnt) {
    return sys_read_writev(fd, iov, iovcnt, false);
}

long sys_access(const char *pathname, int mode) {
    return vfs_check_perms(vfs_open(this->cwd, pathname, false, false), mode);
}

long sys_pipe(int pipefd[2]) {
    return unixpipe_new(pipefd, 0);
}

long sys_pipe2(int pipefd[2], int flags) {
    return unixpipe_new(pipefd, flags);
}

long sys_select() {
    return 0;
}

long sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask) {
    int ready_fds = 0;
    
    fd_set ready_readfds, ready_writefds, ready_exceptfds;
    FD_ZERO(&ready_readfds);
    FD_ZERO(&ready_writefds);
    FD_ZERO(&ready_exceptfds);
    
    for (int fd = 0; fd < nfds; fd++) {
        if (readfds && FD_ISSET(fd, readfds)) {
            if (vfs_poll(fd_get(fd)->node) & POLLIN) {
                FD_SET(fd, &ready_readfds);
                ready_fds++;
            }
        }
        
        if (writefds && FD_ISSET(fd, writefds)) {
            if (vfs_poll(fd_get(fd)->node) & POLLOUT) {
                FD_SET(fd, &ready_writefds);
                ready_fds++;
            }
        }
    }
    
    if (readfds) *readfds = ready_readfds;
    if (writefds) *writefds = ready_writefds;
    if (exceptfds) *exceptfds = ready_exceptfds;
    
    return ready_fds;
}

long sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    if (!pathname)
        return -EFAULT;
    if (dirfd == AT_FDCWD)
        return vfs_check_perms(vfs_open(this->cwd, pathname, false, false), mode);
    struct fd *fd = fd_get(dirfd);
    if (!fd)
        return -EBADF;
    if (pathname[0] != '/')
        return vfs_check_perms(vfs_open(fd->node, pathname, false, false), mode);
    else
        return vfs_check_perms(vfs_open(NULL, pathname, false, false), mode);
}

long sys_dup(int oldfd) {
    int newfd = fd_open("/", 0);
    return fd_dup(oldfd, newfd);
}

long sys_dup2(int oldfd, int newfd) {
    return fd_dup(oldfd, newfd);
}

long sys_dup3(int oldfd, int newfd, int flags) {
    (void)flags;
    return fd_dup(oldfd, newfd);
}

long sys_nanosleep(const struct timespec *duration) {
    if (!duration)
        return -EFAULT;
    sched_sleep((uint64_t)duration->tv_sec * 1000000UL + (uint64_t)duration->tv_nsec / 1000UL);
    return 0;
}

long sys_fork(struct registers *r) {
    return fork(r);
}

long sys_execve(const char *pathname, char *const *argv, char *const *envp) {
    int argc;
    for (argc = 0; argv[argc]; argc++);

    return exec(pathname, argc, argv, envp);
}

long sys_exit(long status) {
    sched_kill(this, status);
    __builtin_unreachable();
}

long sys_wait4(int pid, int *wstatus) {
    if (!this->children->head) {
        return -ECHILD;
    }
    sched_block(TASK_PAUSED);
    *wstatus = this->signal_data;
    return 0;
}

long sys_kill(long pid, int sig) {
    struct process *proc = sched_find_process(pid);
    if (!proc)
        return -ESRCH;
    signal_send(proc, sig, 0);
    return 0;
}

char hostname[256] = "localhost";

long sys_uname(struct utsname *utsname) {
    if (!utsname)
        return -EFAULT;

    strncpy(utsname->sysname, __kernel_name, sizeof utsname->sysname);
    strncpy(utsname->nodename, hostname, sizeof utsname->nodename);
    /* TODO: should use snprintf here */
    sprintf(utsname->release, "%d.%d.%d", __kernel_version_major, __kernel_version_minor, __kernel_version_patch);
    sprintf(utsname->version, "%s %s %s", __kernel_commit_hash, __kernel_build_date, __kernel_build_time);
    return 0;
}

long sys_fcntl(int fd_num, int cmd, long arg) {
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    switch (cmd) {
        case F_DUPFD:
            return sys_dup(fd_num);
        case F_DUPFD_CLOEXEC: {
            int newfd = sys_dup(fd_num);
            fd_get(newfd)->flags |= FD_CLOEXEC;
            return newfd;
        }
        case F_GETFD:
            return fd->flags & FD_CLOEXEC;
        case F_SETFD:
            if (arg & FD_CLOEXEC) {
                fd->flags |= FD_CLOEXEC;
            } else {
                fd->flags &= ~FD_CLOEXEC;
            }
            return 0;
        case F_GETFL:
        case F_SETFL:
            return 0;
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            return -ENOSYS;
        default:
            dprintf("%s:%d: %s: command %d not implemented\n", __FILE__, __LINE__, __func__, cmd);
            return -EINVAL;
    }
}

long sys_getcwd(char *buf, size_t size) {
    char path[MAX_PATH];
    vfs_resolve_path(path, this->cwd);
    if (size < (size_t)strlen(path) + 1)
        return -ENAMETOOLONG;
    strcpy(buf, path);
    return 0;
}

long sys_chdir(const char *path) {
    vfs_node_t *newdir = vfs_open(this->cwd, path, false, false);
    if (!newdir)
        return -ENOENT;
    this->cwd = newdir;
    return 0;
}

long sys_mkdir(const char *pathname, mode_t mode) {
    if (!vfs_open(this->cwd, pathname, true, true)) {
        return -EROFS;
    }
    return 0;
}

long sys_rmdir(const char *pathname, mode_t mode) {
    struct vfs_node *node = vfs_open(this->cwd, pathname, false, true);
    if (!node)
        return -ENOENT;
    if (node->type != VFS_DIRECTORY)
        return -ENOTDIR;
    return vfs_remove_node(node);
}

long sys_unlink(const char *pathname) {
    struct vfs_node *node = vfs_open(this->cwd, pathname, false, false);
    if (!node)
        return -ENOENT;
    int ret = vfs_close(node);
    if (ret < 0)
        return ret;
    return vfs_remove_node(node);
}

long sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
    vfs_node_t *node = vfs_open(this->cwd, pathname, false, false);
    if (!node)
        return -ENOENT;
    if (node->type != VFS_SYMLINK)
        return -EINVAL;
    if (!buf)
        return -EFAULT;
    char name[MAX_PATH];
    vfs_resolve_path(name, node->symlink);
    strncpy(buf, name, bufsiz);
    return 0;
}

long sys_getrlimit(int resource, struct rlimit *rlim) {
    if (!rlim)
        return -EFAULT;

    switch (resource) {
        case RLIMIT_NPROC:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
        case RLIMIT_NOFILE:
            rlim->rlim_cur = USER_MAX_FDS;
            rlim->rlim_max = USER_MAX_FDS;
            break;
        default:
            dprintf("%s:%d: %s: unknown resource %d\n", __FILE__, __LINE__, __func__, resource);
            return -EINVAL;
    }
    return 0;
}

long sys_getuid(void) {
    return 0;
}

long sys_getgid(void) {
    return 0;
}

long sys_geteuid(void) {
    return 0;
}

long sys_getegid(void) {
    return 0;
}

long sys_getppid(void) {
    if (this->parent)
        return this->parent->pid;
    else
        return 1;
}

long sys_getpgid(int pid) {
    if (!pid)
        return this->pid;
    return pid;
}

long sys_setpgid(void) {
    return 0;
}

#define ARCH_SET_FS 0x1002

long sys_arch_prctl(int op, long extra) {
    switch (op) {
        case ARCH_SET_FS:
            write_fs(extra);
            this->fs = extra;
            break;
        default:
            dprintf("%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
    return 0;
}

long sys_sethostname(const char *name, size_t len) {
    if (!name)
        return -EFAULT;
    if (len > sizeof hostname)
        return -EINVAL;
    memcpy(hostname, name, len);
    hostname[len] = 0;
    return 0;
}

long sys_getpid(void) {
    return this->pid;
}

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

#define DT_REG  8
#define DT_BLK  6
#define DT_DIR  4
#define DT_CHR  2
#define DT_UNKNOWN 0

long sys_getdents64(int fd_num, struct linux_dirent64 *dirp, unsigned int count) {
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    struct vfs_node *dir = fd->node;

    if (dir->type != VFS_DIRECTORY) {
        return -ENOTDIR;
    }
    if (!dirp || count == 0) {
        return -EINVAL;
    }

    struct vfs_node *child = dir->children;
    int entries_to_skip = fd->offset;
    
    while (child && entries_to_skip > 0) {
        child = child->next;
        entries_to_skip--;
    }
    
    int offset = 0;
    struct linux_dirent64 *current_entry = dirp;
    
    while (child) {
        const char *name = child->name;
        int name_len = strlen(name);
        int reclen = ALIGN_UP(sizeof(struct linux_dirent64) + name_len + 1, 8);
        
        if ((unsigned)(offset + reclen) > count)
            break;
        
        current_entry->d_ino = child->inode;
        current_entry->d_off = fd->offset + 1;
        current_entry->d_reclen = reclen;
        switch (child->type) {
            case VFS_DIRECTORY:
                current_entry->d_type = DT_DIR;
                break;
            case VFS_FILE:
                current_entry->d_type = DT_REG;
                break;
            case VFS_CHARDEVICE:
                current_entry->d_type = DT_CHR;
                break;
            case VFS_BLOCKDEVICE:
                current_entry->d_type = DT_BLK;
                break;
            default:
                current_entry->d_type = DT_UNKNOWN;
                break;
        }
        strcpy(current_entry->d_name, name);
        
        current_entry = (void*)current_entry + reclen;
        offset += reclen;
        child = child->next;
        fd->offset++;
    }
    
    return offset;
}

long sys_set_tid_address(int *tidptr) {
    return this->pid;
}

long sys_clock_gettime(int clockid, struct timespec *tp) {
    (void)clockid;
    if (!tp)
        return -EFAULT;

    switch (clockid) {
        case CLOCK_REALTIME:
            gettimeofday(&tp->tv_sec, &tp->tv_nsec);
            break;
        case CLOCK_MONOTONIC:
            if (hpet) hpet_read_time(&tp->tv_sec, &tp->tv_nsec);
            else tsc_read_time(&tp->tv_sec, &tp->tv_nsec);
            break;
        default:    
            dprintf("%s:%d: unknown clockid %d\n", __FILE__, __LINE__, clockid);
            return -EINVAL;
    }
    return 0;
}

long sys_utimensat() {
    unimplemented;
    return -ENOENT;
}

typedef long (*syscall_func)(long, long, long, long, long, long);

static syscall_func syscalls[] = {
    [SYS_read]              = (syscall_func)(uintptr_t)sys_read,
    [SYS_write]             = (syscall_func)(uintptr_t)sys_write,
    [SYS_open]              = (syscall_func)(uintptr_t)sys_open,
    [SYS_close]             = (syscall_func)(uintptr_t)sys_close,
    [SYS_stat]              = (syscall_func)(uintptr_t)sys_stat,
    [SYS_fstat]             = (syscall_func)(uintptr_t)sys_fstat,
    [SYS_lstat]             = (syscall_func)(uintptr_t)sys_lstat,
    [SYS_lseek]             = (syscall_func)(uintptr_t)sys_lseek,
    [SYS_mmap]              = (syscall_func)(uintptr_t)sys_mmap,
    [SYS_munmap]            = (syscall_func)(uintptr_t)sys_munmap,
    [SYS_brk]               = (syscall_func)(uintptr_t)sys_brk,
    [SYS_rt_sigaction]      = (syscall_func)(uintptr_t)sys_rt_sigaction,
    [SYS_rt_sigprocmask]    = (syscall_func)(uintptr_t)sys_rt_sigprocmask,
    [SYS_ioctl]             = (syscall_func)(uintptr_t)sys_ioctl,
    [SYS_readv]             = (syscall_func)(uintptr_t)sys_readv,
    [SYS_writev]            = (syscall_func)(uintptr_t)sys_writev,
    [SYS_access]            = (syscall_func)(uintptr_t)sys_access,
    [SYS_pipe]              = (syscall_func)(uintptr_t)sys_pipe,
    [SYS_select]            = (syscall_func)(uintptr_t)sys_select,
    [SYS_dup]               = (syscall_func)(uintptr_t)sys_dup,
    [SYS_dup2]              = (syscall_func)(uintptr_t)sys_dup2,
    [SYS_nanosleep]         = (syscall_func)(uintptr_t)sys_nanosleep,
    [SYS_getpid]            = (syscall_func)(uintptr_t)sys_getpid,
    [SYS_fork]              = (syscall_func)(uintptr_t)sys_fork,
    [SYS_execve]            = (syscall_func)(uintptr_t)sys_execve,
    [SYS_exit]              = (syscall_func)(uintptr_t)sys_exit,
    [SYS_wait4]             = (syscall_func)(uintptr_t)sys_wait4,
    [SYS_kill]              = (syscall_func)(uintptr_t)sys_kill,
    [SYS_uname]             = (syscall_func)(uintptr_t)sys_uname,
    [SYS_fcntl]             = (syscall_func)(uintptr_t)sys_fcntl,
    [SYS_getcwd]            = (syscall_func)(uintptr_t)sys_getcwd,
    [SYS_chdir]             = (syscall_func)(uintptr_t)sys_chdir,
    [SYS_mkdir]             = (syscall_func)(uintptr_t)sys_mkdir,
    [SYS_rmdir]             = (syscall_func)(uintptr_t)sys_rmdir,
    [SYS_unlink]            = (syscall_func)(uintptr_t)sys_unlink,
    [SYS_readlink]          = (syscall_func)(uintptr_t)sys_readlink,
    [SYS_getrlimit]         = (syscall_func)(uintptr_t)sys_getrlimit,
    [SYS_getuid]            = (syscall_func)(uintptr_t)sys_getuid,
    [SYS_getgid]            = (syscall_func)(uintptr_t)sys_getgid,
    [SYS_geteuid]           = (syscall_func)(uintptr_t)sys_geteuid,
    [SYS_getegid]           = (syscall_func)(uintptr_t)sys_getegid,
    [SYS_setppid]           = (syscall_func)(uintptr_t)sys_setpgid,
    [SYS_getppid]           = (syscall_func)(uintptr_t)sys_getppid,
    [SYS_getpgid]           = (syscall_func)(uintptr_t)sys_getpgid,
    [SYS_arch_prctl]        = (syscall_func)(uintptr_t)sys_arch_prctl,
    [SYS_sethostname]       = (syscall_func)(uintptr_t)sys_sethostname,
    [SYS_gettid]            = (syscall_func)(uintptr_t)sys_getpid,
    [SYS_getdents64]        = (syscall_func)(uintptr_t)sys_getdents64,
    [SYS_set_tid_address]   = (syscall_func)(uintptr_t)sys_set_tid_address,
    [SYS_clock_gettime]     = (syscall_func)(uintptr_t)sys_clock_gettime,
    [SYS_exit_group]        = (syscall_func)(uintptr_t)sys_exit,
    [SYS_newfstatat]        = (syscall_func)(uintptr_t)sys_newfstatat,
    [SYS_faccessat]         = (syscall_func)(uintptr_t)sys_faccessat,
    [SYS_pselect6]          = (syscall_func)(uintptr_t)sys_pselect6,
    [SYS_utimensat]         = (syscall_func)(uintptr_t)sys_utimensat,
    [SYS_dup3]              = (syscall_func)(uintptr_t)sys_dup3,
    [SYS_pipe2]             = (syscall_func)(uintptr_t)sys_pipe2
};

void syscall_handler(struct registers *r) {
    if (r->rax >= sizeof syscalls / sizeof(void *) || !syscalls[r->rax]) {
        dprintf("%s:%d: unknown syscall %lu\n", __FILE__, __LINE__, r->rax);
        r->rax = -ENOSYS;
        sched_unlock();
        return;
    }

    syscall_func handler = syscalls[r->rax];
    r->rax = handler((r->rax == SYS_fork) ? (long)r : r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9);
}