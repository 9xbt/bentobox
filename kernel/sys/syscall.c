//#include <asm-generic/resource.h>
//#include <linux/resource.h>
//#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>
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
#include <kernel/socket.h>
#include <kernel/errno.h>
#include <kernel/sched.h>
#include <kernel/video.h>
#include <kernel/time.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/fd.h>

static long sys_read_write(int fd_num, void *buffer, size_t len, bool write) {
    struct fd *fd = fd_get(fd_num);
    if (!fd || !fd->open)
        return -EBADFD;
    if (!fd->node)
        return -ENOENT;
    if ((!fd->node->write && write) || (!fd->node->read && !write))
        return 0;

    if (!(fd->flags & O_NONBLOCK)) {
        vfs_poll(fd->node, write ? POLLOUT : POLLIN, -1);
    }

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

long sys_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    (void)mode;
    if (!pathname)
        return -EFAULT;

    struct vfs_node *node = NULL;
    if (pathname[0] == '/') {
        node = vfs_open(this->cwd, pathname, true, false);
    } else if (dirfd == AT_FDCWD) {
        node = vfs_open(this->cwd, pathname, true, false);
    } else {
        struct fd *dir_fd = fd_get(dirfd);
        if (!dir_fd)
            return -EBADF;
        if (dir_fd->node->type != VFS_DIRECTORY)
            return -ENOTDIR;
        node = vfs_open(dir_fd->node, pathname, true, false);
    }

    return fd_create(node, flags);
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
    return vfs_stat(vfs_open(this->cwd, pathname, false, true), statbuf, true);
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
        struct fd *dir_fd = fd_get(dirfd);
        if (!dir_fd)
            return -EBADF;
        if (dir_fd->node->type != VFS_DIRECTORY)
            return -ENOTDIR;
        node = vfs_open(dir_fd->node, pathname, false, false);
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

        /** TODO: fix mmap returning this when mapping with PROT_NONE */
        if (!ptr) return -ENOMEM;

        if (prot != PROT_NONE) memset(ptr, 0, pages * PAGE_SIZE);

        return (long)ptr;
    }

    struct fd *fd = fd_get(fd_num);
    if (!fd || !fd->node->mmap)
        return -ENODEV;
    return fd->node->mmap(fd->node, addr, length, prot, flags, offset);
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
        dprintf(LOG_INFO, "%s:%d: %s: TODO: shrinking\n", __FILE__, __LINE__, __func__);
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

static long sys_read_writev(int fd_num, const struct iovec *iov, int iovcnt, bool write) {
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

        if (!(fd->flags & O_NONBLOCK)) {
            vfs_poll(fd->node, write ? POLLOUT : POLLIN, -1);
        }
        
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

long sys_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    int ready = 0;
    for (nfds_t fd_num = 0; fd_num < nfds; fd_num++) {
        struct pollfd *pfd = &fds[fd_num];
        pfd->revents = 0;

        struct fd *fd = fd_get(pfd->fd);
        if (!fd) {
            pfd->revents = POLLNVAL;
            continue;
        }

        if ((pfd->revents = vfs_poll(fd->node, pfd->events, timeout)))
            ready++;
    }
    return ready;
}

long sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *ts, const sigset_t *sigmask) {
    int ready = 0;
    
    fd_set ready_readfds, ready_writefds, ready_exceptfds;
    FD_ZERO(&ready_readfds);
    FD_ZERO(&ready_writefds);
    FD_ZERO(&ready_exceptfds);
    
    long timeout = ts ? (ts->tv_sec * 1000000L + ts->tv_nsec / 1000) : -1;
    for (int fd = 0; fd < nfds; fd++) {
        if (readfds && FD_ISSET(fd, readfds)) {
            if (vfs_poll(fd_get(fd)->node, POLLIN, timeout) & POLLIN) {
                FD_SET(fd, &ready_readfds);
                ready++;
            }
        }
        
        if (writefds && FD_ISSET(fd, writefds)) {
            if (vfs_poll(fd_get(fd)->node, POLLOUT, timeout) & POLLOUT) {
                FD_SET(fd, &ready_writefds);
                ready++;
            }
        }
    }
    
    if (readfds) *readfds = ready_readfds;
    if (writefds) *writefds = ready_writefds;
    if (exceptfds) *exceptfds = ready_exceptfds;
    
    return ready;
}

long sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    //return nfds;
    struct timespec ts;
    if (timeout) {
        ts.tv_sec = timeout->tv_sec;
        ts.tv_nsec = timeout->tv_usec * 1000;
    }
    return sys_pselect6(nfds, readfds, writefds, exceptfds, timeout ? &ts : NULL, NULL);
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

long sys_wait4(int pid, int *wstatus, int options, void *rusage);

long sys_clone(unsigned long flags, unsigned long newsp, int *parent_tidptr, int *child_tidptr, unsigned long tls) {
    if ((flags & (CLONE_VM | CLONE_VFORK)) == (CLONE_VM | CLONE_VFORK)) {
        long pid = fork(this->syscall_ctx, newsp);
        if (pid == 0) return 0;

        int status;
        sys_wait4(pid, &status, 0, NULL);
        return pid;
    }

    if (!(flags & CLONE_VM)) {
        return fork(this->syscall_ctx, this->stack);
    }

    dprintf(LOG_ERR, "%s:%d: unsupported flags 0x%lx\n", __FILE__, __LINE__, flags);
    return -ENOSYS;
}

long sys_fork(void) {
    return fork(this->syscall_ctx, this->stack);
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

long sys_wait4(int pid, int *wstatus, int options, void *rusage) {
    (void)options;
    (void)rusage;

    if (!this->children->head) {
        return -ECHILD;
    }
    sched_block(TASK_PAUSED);
    if (wstatus)
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
    sprintf(utsname->machine, "%s", __kernel_arch);
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
            fd_get(newfd)->flags |= O_CLOEXEC;
            return newfd;
        }
        case F_GETFD:
            return fd->flags & O_CLOEXEC;
        case F_SETFD:
            if (arg & FD_CLOEXEC) {
                fd->flags |= O_CLOEXEC;
            } else {
                fd->flags &= ~O_CLOEXEC;
            }
            return 0;
        case F_GETFL:
        case F_SETFL:
            return 0;
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            return -ENOSYS;
        case 1032: /* F_GETPIPE_SZ */
            if (fd->node->type != VFS_UNIXPIPE)
                return -EINVAL;
            return UNIXPIPE_BUFFER_SIZE;
        default:
            dprintf(LOG_INFO, "%s:%d: %s: command %d not implemented\n", __FILE__, __LINE__, __func__, cmd);
            return -EINVAL;
    }
}

long sys_getcwd(char *buf, size_t size) {
    char path[MAX_PATH];
    vfs_resolve_path(path, this->cwd);
    if (size < (size_t)strlen(path) + 1)
        return -ENAMETOOLONG;
    strcpy(buf, path);
    return strlen(path);
}

long sys_chdir(const char *path) {
    vfs_node_t *newdir = vfs_open(this->cwd, path, false, false);
    if (!newdir)
        return -ENOENT;
    this->cwd = newdir;
    return 0;
}

long sys_fchdir(int fd_num) {
    struct fd *fd = fd_get(fd_num);
    if (!fd)
        return -EBADF;
    this->cwd = fd->node;
    return 0;
}

long sys_mkdir(const char *pathname, mode_t mode) {
    if (!vfs_open(this->cwd, pathname, true, true)) {
        return -EROFS;
    }
    return 0;
}

long sys_mkdirat(int dirfd, const char *pathname, int flags, mode_t mode) {
    (void)mode;
    if (!pathname)
        return -EFAULT;

    struct vfs_node *node = NULL;
    if (pathname[0] == '/') {
        node = vfs_open(this->cwd, pathname, true, true);
    } else if (dirfd == AT_FDCWD) {
        node = vfs_open(this->cwd, pathname, true, true);
    } else {
        struct fd *dir_fd = fd_get(dirfd);
        if (!dir_fd)
            return -EBADF;
        if (dir_fd->node->type != VFS_DIRECTORY)
            return -ENOTDIR;
        node = vfs_open(dir_fd->node, pathname, true, true);
    }

    int ret = fd_create(node, flags);
    if (ret < 0)
        return ret;
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
    struct vfs_node *node = vfs_open(this->cwd, pathname, false, true);
    if (!node)
        return -ENOENT;
    int ret = vfs_close(node);
    if (ret < 0)
        return ret;
    return vfs_remove_node(node);
}

long sys_unlinkat(int dirfd, const char *pathname, int flags) {
    (void)flags;
    if (!pathname)
        return -EFAULT;

    struct vfs_node *node = NULL;
    if (pathname[0] == '/') {
        node = vfs_open(this->cwd, pathname, false, false);
    } else if (dirfd == AT_FDCWD) {
        node = vfs_open(this->cwd, pathname, false, false);
    } else {
        struct fd *dir_fd = fd_get(dirfd);
        if (!dir_fd)
            return -EBADF;
        if (dir_fd->node->type != VFS_DIRECTORY)
            return -ENOTDIR;
        node = vfs_open(dir_fd->node, pathname, false, false);
    }

    int ret = vfs_close(node);
    if (ret < 0)
        return ret;
    return vfs_remove_node(node);
}

long sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
    vfs_node_t *node = vfs_open(this->cwd, pathname, false, true);
    if (!node)
        return -ENOENT;
    if (node->type != VFS_SYMLINK)
        return -EINVAL;
    if (!buf)
        return -EFAULT;

    vfs_node_t *target = vfs_resolve_symlink(node, MAX_NESTED_SYMLINKS);
    if (!target)
        return -ENOENT;

    char name[MAX_PATH];
    vfs_resolve_path(name, target);
    strncpy(buf, name, bufsiz);
    return strlen(name);
}

long sys_getrlimit(unsigned int resource, struct rlimit *rlim) {
    if (!rlim)
        return -EFAULT;

    switch (resource) {
        case RLIMIT_DATA:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
        case RLIMIT_NPROC:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
        case RLIMIT_NOFILE:
            rlim->rlim_cur = USER_MAX_FDS;
            rlim->rlim_max = USER_MAX_FDS;
            break;
        default:
            dprintf(LOG_INFO, "%s:%d: %s: unknown resource %d\n", __FILE__, __LINE__, __func__, resource);
            return -EINVAL;
    }
    return 0;
}

long sys_sysinfo(struct sysinfo *info) {
    if (!info)
        return -EFAULT;
    uptime(&info->uptime, NULL);
    info->totalram = mmu_usable_mem / 1024;
    info->freeram = mmu_usable_mem / 1024 - mmu_used_pages * 4;
    info->sharedram = 0;
    info->bufferram = 0;
    info->totalswap = 0;
    info->freeswap = 0;
    info->procs = 0;
    info->totalhigh = 0;
    info->freehigh = 0;
    info->mem_unit = 1024;
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

long sys_setsid(void) {
    return 1;
}

long sys_getpgid(int pid) {
    struct process *proc = pid ? sched_find_process(pid) : this;
    if (!proc)
        return -ESRCH;
    return proc->pgid;
}

long sys_setpgid(int pid, int pgid) {
    struct process *proc = pid ? sched_find_process(pid) : this;
    if (!proc)
        return -ESRCH;
    proc->pgid = pgid ? pgid : pid;
    return 0;
}

long sys_mknod(const char *pathname, mode_t mode, dev_t dev) {
    mode_t type = mode & S_IFMT;
    switch (type) {
        case S_IFIFO:
            return fifo_new(pathname);
        default:
            dprintf(LOG_NOTICE, "%s:%d: %s: unknown type %u\n", __FILE__, __LINE__, __func__, type);
            return -EINVAL;
    }
}

#define ARCH_SET_FS 0x1002

long sys_arch_prctl(int op, long extra) {
    switch (op) {
        case ARCH_SET_FS:
            write_fs(extra);
            this->fs = extra;
            break;
        default:
            dprintf(LOG_INFO, "%s:%d: %s: function 0x%lx not implemented\n", __FILE__, __LINE__, __func__, op);
            return -EINVAL;
    }
    return 0;
}

long sys_mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data) {
    (void)data;
    if (!source || !target || !filesystemtype)
        return -EFAULT;
    return vfs_mount(
        vfs_open(this->cwd, source, false, false),
        vfs_open(this->cwd, target, false, false),
        filesystemtype,
        mountflags);
}

#define LINUX_REBOOT_MAGIC1     0xfee1dead
#define LINUX_REBOOT_MAGIC2     0x28121969
#define LINUX_REBOOT_MAGIC2A    0x05121996
#define LINUX_REBOOT_MAGIC2B    0x16041998
#define LINUX_REBOOT_MAGIC2C    0x20112000

#define LINUX_REBOOT_CMD_RESTART    0x1234567
#define LINUX_REBOOT_CMD_HALT       0xCDEF0123
#define LINUX_REBOOT_CMD_POWER_OFF  0x4321FEDC

extern void arch_prepare_fatal(void);
extern void arch_fatal(void);

long sys_reboot(unsigned int magic, unsigned int magic2, int op, void *arg) {
    if (magic != LINUX_REBOOT_MAGIC1)
        return -EINVAL;

    if (magic2 != LINUX_REBOOT_MAGIC2 &&
        magic2 != LINUX_REBOOT_MAGIC2A &&
        magic2 != LINUX_REBOOT_MAGIC2B &&
        magic2 != LINUX_REBOOT_MAGIC2C)
        return -EINVAL;

    switch (op) {
        case LINUX_REBOOT_CMD_RESTART:
            acpi_reboot();
            __builtin_unreachable();
        default:
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

long sys_socket(int domain, int type, int protocol) {
    switch (domain) {
        case PF_UNIX:
            return unixsocket_new(type);
        default:
            dprintf(LOG_NOTICE, "%s:%d: unknown socket domain %d\n", __FILE__, __LINE__, domain);
            return -ENOSYS;
    }
}

struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

#define DT_LNK 10
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
    if (dir->type != VFS_DIRECTORY)
        return -ENOTDIR;

    if (!dirp || count == 0)
        return -EINVAL;

    int offset = 0;
    int skip = fd->offset;
    struct linux_dirent64 *entry = dirp;

    foreach(item, dir->children) {
        if (skip > 0) {
            skip--;
            continue;
        }

        struct vfs_node *child = item->value;
        const char *name = child->name;
        int name_len = strlen(name);
        int reclen = ALIGN_UP(sizeof(struct linux_dirent64) + name_len + 1, 8);

        if ((unsigned)(offset + reclen) > count)
            break;

        entry->d_ino = child->inode;
        entry->d_off = fd->offset + 1;
        entry->d_reclen = reclen;

        switch (child->type) {
            case VFS_DIRECTORY:
                entry->d_type = DT_DIR;
                break;
            case VFS_FILE:
                entry->d_type = DT_REG;
                break;
            case VFS_CHARDEVICE:
                entry->d_type = DT_CHR;
                break;
            case VFS_BLOCKDEVICE:
                entry->d_type = DT_BLK;
                break;
            case VFS_SYMLINK:
                entry->d_type = DT_LNK;
                break;
            default:
                entry->d_type = DT_UNKNOWN;
                break;
        }

        strcpy(entry->d_name, name);
        entry = (void *)entry + reclen;
        offset += reclen;
        fd->offset++;
    }

    return offset;
}

long sys_set_tid_address(int *tidptr) {
    return this->pid;
}

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

long sys_clock_gettime(int clockid, struct timespec *tp) {
    (void)clockid;
    if (!tp)
        return -EFAULT;

    switch (clockid) {
        case CLOCK_REALTIME:
            gettimeofday(&tp->tv_sec, &tp->tv_nsec);
            break;
        case CLOCK_MONOTONIC:
            //if (hpet) hpet_read_time(&tp->tv_sec, &tp->tv_nsec);
            //else tsc_read_time(&tp->tv_sec, &tp->tv_nsec);
            uptime(&tp->tv_sec, &tp->tv_nsec);
            break;
        default:    
            dprintf(LOG_INFO, "%s:%d: unknown clockid %d\n", __FILE__, __LINE__, clockid);
            return -EINVAL;
    }
    return 0;
}

long sys_utimensat() {
    unimplemented;
    return -ENOENT;
}

long sys_prlimit64(long pid, unsigned int resource, void *new_rlim, void *old_rlim) {
    if (!new_rlim || !old_rlim)
        return -EFAULT;
    if (pid != this->pid) {
        dprintf(LOG_NOTICE, "%s:%d: TODO: do prlimit on requested PID (%ld)\n", __FILE__, __LINE__, pid);
        return -ESRCH;
    }
    switch (resource) {
        default:
            dprintf(LOG_INFO, "%s:%d: %s: unknown resource %d\n", __FILE__, __LINE__, __func__, resource);
            return -EINVAL;
    }
    return 0;
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
    [SYS_poll]              = (syscall_func)(uintptr_t)sys_poll,
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
    [SYS_socket]            = (syscall_func)(uintptr_t)sys_socket,
    [SYS_clone]             = (syscall_func)(uintptr_t)sys_clone,
    [SYS_fork]              = (syscall_func)(uintptr_t)sys_fork,
    [SYS_execve]            = (syscall_func)(uintptr_t)sys_execve,
    [SYS_exit]              = (syscall_func)(uintptr_t)sys_exit,
    [SYS_wait4]             = (syscall_func)(uintptr_t)sys_wait4,
    [SYS_kill]              = (syscall_func)(uintptr_t)sys_kill,
    [SYS_uname]             = (syscall_func)(uintptr_t)sys_uname,
    [SYS_fcntl]             = (syscall_func)(uintptr_t)sys_fcntl,
    [SYS_getcwd]            = (syscall_func)(uintptr_t)sys_getcwd,
    [SYS_chdir]             = (syscall_func)(uintptr_t)sys_chdir,
    [SYS_fchdir]            = (syscall_func)(uintptr_t)sys_fchdir,
    [SYS_mkdir]             = (syscall_func)(uintptr_t)sys_mkdir,
    [SYS_rmdir]             = (syscall_func)(uintptr_t)sys_rmdir,
    [SYS_unlink]            = (syscall_func)(uintptr_t)sys_unlink,
    [SYS_readlink]          = (syscall_func)(uintptr_t)sys_readlink,
    [SYS_getrlimit]         = (syscall_func)(uintptr_t)sys_getrlimit,
    [SYS_sysinfo]           = (syscall_func)(uintptr_t)sys_sysinfo,
    [SYS_getuid]            = (syscall_func)(uintptr_t)sys_getuid,
    [SYS_getgid]            = (syscall_func)(uintptr_t)sys_getgid,
    [SYS_geteuid]           = (syscall_func)(uintptr_t)sys_geteuid,
    [SYS_getegid]           = (syscall_func)(uintptr_t)sys_getegid,
    [SYS_setppid]           = (syscall_func)(uintptr_t)sys_setpgid,
    [SYS_getppid]           = (syscall_func)(uintptr_t)sys_getppid,
    [SYS_setsid]            = (syscall_func)(uintptr_t)sys_setsid,
    [SYS_getpgid]           = (syscall_func)(uintptr_t)sys_getpgid,
    [SYS_mknod]             = (syscall_func)(uintptr_t)sys_mknod,
    [SYS_arch_prctl]        = (syscall_func)(uintptr_t)sys_arch_prctl,
    [SYS_mount]             = (syscall_func)(uintptr_t)sys_mount,
    [SYS_reboot]            = (syscall_func)(uintptr_t)sys_reboot,
    [SYS_sethostname]       = (syscall_func)(uintptr_t)sys_sethostname,
    [SYS_gettid]            = (syscall_func)(uintptr_t)sys_getpid,
    [SYS_getdents64]        = (syscall_func)(uintptr_t)sys_getdents64,
    [SYS_set_tid_address]   = (syscall_func)(uintptr_t)sys_set_tid_address,
    [SYS_clock_gettime]     = (syscall_func)(uintptr_t)sys_clock_gettime,
    [SYS_exit_group]        = (syscall_func)(uintptr_t)sys_exit,
    [SYS_openat]            = (syscall_func)(uintptr_t)sys_openat,
    [SYS_mkdirat]           = (syscall_func)(uintptr_t)sys_mkdirat,
    [SYS_newfstatat]        = (syscall_func)(uintptr_t)sys_newfstatat,
    [SYS_unlinkat]          = (syscall_func)(uintptr_t)sys_unlinkat,
    [SYS_faccessat]         = (syscall_func)(uintptr_t)sys_faccessat,
    [SYS_pselect6]          = (syscall_func)(uintptr_t)sys_pselect6,
    [SYS_utimensat]         = (syscall_func)(uintptr_t)sys_utimensat,
    [SYS_dup3]              = (syscall_func)(uintptr_t)sys_dup3,
    [SYS_pipe2]             = (syscall_func)(uintptr_t)sys_pipe2,
    [SYS_prlimit64]         = (syscall_func)(uintptr_t)sys_prlimit64
};

void syscall_handler(struct registers *r) {
    this->syscall_ctx = r;
    if (r->rax >= sizeof syscalls / sizeof(void *) || !syscalls[r->rax]) {
        dprintf(LOG_NOTICE, "%s:%d: unknown syscall %lu\n", __FILE__, __LINE__, r->rax);
        r->rax = -ENOSYS;
        sched_unlock();
        return;
    }

    syscall_func handler = syscalls[r->rax];
    r->rax = handler(r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9);
}