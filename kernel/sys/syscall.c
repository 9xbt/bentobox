#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/unixpipe.h>
#include <kernel/syscall.h>
#include <kernel/version.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/socket.h>
#include <kernel/errno.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/futex.h>
#include <kernel/acpi.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>

static long sys_read_write(int fd, void *buf, size_t len, bool write, bool poll) {
    struct file *file = file_get(fd);
    if (!file || !file->open)
        return -EBADFD;
    if (!file->node)
        return -ENOENT;
    if (!len)
        return 0;

    void *buffer = kmalloc(len);
    if (write && copy_from_user(buffer, buf, len) < 0) {
        kfree(buffer);
        return -EFAULT;
    }

    if (!(file->flags & O_NONBLOCK) && poll) {
    retry:
        while (!(vfs_poll(file->node, write ? POLLOUT : POLLIN, -1) & (write ? POLLOUT : POLLIN)));
    }

    long ret = write ?
        vfs_write(file->node, buffer, file->offset, len) :
        vfs_read(file->node, buffer, file->offset, len);

    if (ret == -EAGAIN && !(file->flags & O_NONBLOCK) && poll)
        goto retry;
    
    if (ret < 0) {
        kfree(buffer);
        return ret;
    }
    file->offset += ret;

    if (!write && copy_to_user(buf, buffer, ret) < 0) {
        kfree(buffer);
        return -EFAULT;
    }
    kfree(buffer);
    return ret;
}

long sys_read(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, false, true);
}

long sys_write(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, true, true);
}

#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2
#define SEEK_DATA   3
#define SEEK_HOLE   4

long sys_seek(int fd, long offset, int whence) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (file->node->type == VFS_CHARDEVICE)
        return -ESPIPE;

    switch (whence) {
        case SEEK_SET:
            file->offset = offset;
            break;
        case SEEK_CUR:
            file->offset += offset;
            break;
        case SEEK_END:
            file->offset = file->node->size + offset;
            break;
        case SEEK_DATA:
            if (offset >= (long)file->node->size)
                return -ENXIO;
            file->offset = offset;
            break;
        case SEEK_HOLE:
            if (offset >= (long)file->node->size)
                return -ENXIO;
            file->offset = file->node->size;
            break;
        default:
            dprintf(LOG_DEBUG, "\033[93m%s:\033[0m: invalid whence %d\n", __func__, whence);
            return -EINVAL;
    }

    return file->offset;
}

long sys_openat(int dirfd, const char *pathname, int flags, unsigned int mode) {
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, pathname, MAX_PATH);
    int fd = file_open(dir, path, flags, mode);
    kfree(path);
    return fd;
}

long sys_close(int fd) {
    return file_close(fd);
}

long sys_fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, pathname, MAX_PATH);
    vfs_node_t *node = vfs_lookup(dir, path, (flags & AT_SYMLINK_NOFOLLOW) ? false : true, VFS_NONE);
    kfree(path);
    if (!node)
        return -ENOENT;

    struct stat st;
    memset(&st, 0, sizeof st);
    switch (node->type) {
        case VFS_FILE:
            st.st_mode |= S_IFREG;
            break;
        case VFS_DIRECTORY:
            st.st_mode |= S_IFDIR;
            break;
        case VFS_CHARDEVICE:
            st.st_mode |= S_IFCHR;
            break;
        case VFS_BLOCKDEVICE:
            st.st_mode |= S_IFBLK;
            break;
        case VFS_SYMLINK:
            st.st_mode |= S_IFLNK;
            break;
        default:
            st.st_mode |= S_IFREG;
            break;
    }
    st.st_mode |= (node->perms & 07777);

    st.st_nlink = 1;
    st.st_uid = 0;
    st.st_gid = 0;
    st.st_ino = node->inode;
    st.st_atim.tv_sec = node->atime;
    st.st_ctim.tv_sec = node->ctime;
    st.st_mtim.tv_sec = node->mtime;
    
    switch (node->type) {
        case VFS_FILE:
        case VFS_DIRECTORY:
            st.st_size = node->size;
            st.st_blocks = node->blocks;
            break;
        case VFS_SYMLINK:
            st.st_size = node->size;
            break;
        default:
            st.st_size = 0;
            break;
    }

    return copy_to_user(statbuf, &st, sizeof st);
}

long sys_ioctl(int fd, int op, void *arg) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->tty_ops || !file->node->tty_ops->ioctl)
        return -ENOTTY;
    return file->node->tty_ops->ioctl(file->node, op, arg);
}

long sys_dup(int oldfd, int newfd, int flags) {
    return file_dup(oldfd, newfd, flags, newfd >= 0);
}

#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define F_GETLK     5
#define F_SETLK     6
#define F_SETLKW    7

#define F_DUPFD_CLOEXEC 1030

#define FD_CLOEXEC  1

long sys_fcntl(int fd, int op, long arg) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;

    switch (op) {
        case F_DUPFD:
            return file_dup(fd, arg, 0, false);
        case F_DUPFD_CLOEXEC:
            return file_dup(fd, arg, O_CLOEXEC, false);
        case F_GETFD:
            return file->flags & O_CLOEXEC;
        case F_SETFD:
            if (arg & FD_CLOEXEC)
                file->flags |= O_CLOEXEC;
            else
                file->flags &= ~O_CLOEXEC;
            return 0;
        case F_GETFL:
            return file->flags;
        case F_SETFL:
            file->flags = arg;
            return 0;
        default:
            dprintf(LOG_DEBUG, "%s: function %d not implemented\n", __func__, op);
            return -EINVAL;
    }
}

struct dirent {
    long           d_ino;
    long           d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

#define DT_UNKNOWN  0
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10

long sys_readdir(int fd, struct dirent *buf, size_t count) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    
    vfs_node_t *dir = file->node;
    if (dir->type != VFS_DIRECTORY)
        return -ENOTDIR;
    if (!count)
        return -EINVAL;
    
    size_t size = count < PAGE_SIZE ? count : PAGE_SIZE;
    struct dirent *dirent = kmalloc(size);
    
    size_t offset = 0;
    int skip = file->offset;
    
    foreach(j, dir->children) {
        if (skip > 0) {
            skip--;
            continue;
        }
        
        vfs_node_t *node = j->value;
        
        size_t reclen = ALIGN_UP(sizeof(struct dirent) + strlen(node->name) + 1, 8);
        if (offset + reclen > size)
            break;
        
        struct dirent *entry = (struct dirent *)((char *)dirent + offset);
        entry->d_ino = node->inode;
        entry->d_off = file->offset + 1;
        entry->d_reclen = reclen;
        switch (node->type) {
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
        
        strcpy(entry->d_name, node->name);
        
        offset += reclen;
        file->offset++;
    }
    
    if (copy_to_user(buf, dirent, offset) < 0) {
        kfree(dirent);
        return -EFAULT;
    }
    
    kfree(dirent);
    return offset;
}

long sys_exit(int status) {
    sched_exit_group(this_proc, (status & 0xff) << 8);
    __builtin_unreachable();
}

#define WNOHANG 1

long sys_waitpid(int pid, int *wstatus, int options) {
    if (this_proc->children->length == 0 && this_proc->dead_children->length == 0)
        return -ECHILD;
    
    for (;;) {
        if (this_proc->dead_children->length > 0) {
            struct dead_process *dp = NULL;
            
            if (pid > 0) {
                foreach_safe(i, this_proc->dead_children) {
                    struct dead_process *d = i->value;
                    if (d->pid == pid) {
                        dp = d;
                        list_remove(this_proc->dead_children, i);
                        break;
                    }
                }
            } else if (pid == -1) {
                dp = list_pop(this_proc->dead_children);
            }
            // TODO: handle pid == 0 & pid < -1
            
            if (dp) {
                *wstatus = dp->status;
                int ret_pid = dp->pid;
                kfree(dp);
                return ret_pid;
            }
        }

        if (options & WNOHANG)
            return 0;

        this->state = THREAD_PAUSED;
        sched_yield();
    }
    return pid;
}

long sys_kill(int pid, int sig) {
    if (pid > 0) {
        struct process *proc = sched_find_process(pid);
        if (!proc)
            return -ESRCH;
        return signal_send(proc, sig);
    } else if (pid == 0) {
        return signal_send_pgrp(this_proc->pgid, sig);
    } else if (pid < -1) {
        return signal_send_pgrp(-pid, sig);
    }
    return -EINVAL;
}

long sys_fork(void) {
    return fork();
}

long sys_exec(const char *filename, char *argv[], char *envp[]) {
    int argc = 0;
    if (argv) for (; argv[argc]; argc++);
    return exec(filename, argc, argv, envp);
}

long sys_getpid(void) {
    return this_proc->pid;
}

long sys_gettid(void) {
    return this->tid;
}

long sys_getppid(void) {
    return this->parent ? this->parent->pid : 1;
}

long sys_getpgid(long pid) {
    struct process *proc = pid ? sched_find_process(pid) : this_proc;
    if (!proc)
        return -ESRCH;
    return proc->pgid;
}

long sys_setpgid(long pid, long pgid) {
    struct process *proc = pid ? sched_find_process(pid) : this_proc;
    if (!proc)
        return -ESRCH;
    proc->pgid = pgid;
    return 0;
}

long sys_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    if (!length)
        return -EINVAL;

    uint64_t mmu_flags = 0;
    if (prot != PROT_NONE) {
        #ifdef __x86_64__
        mmu_flags = PTE_USER;
        if (prot & PROT_READ) mmu_flags |= PTE_PRESENT;
        if (prot & PROT_WRITE) mmu_flags |= PTE_WRITABLE;
        if (!(prot & PROT_EXEC)) mmu_flags |= PTE_NX;
        #elif __aarch64__
        mmu_flags = PTE_VALID | PTE_AF | (prot & PROT_WRITE ? PTE_USER_RW : PTE_USER_RO);
        if (!(prot & PROT_EXEC)) mmu_flags |= PTE_UXN;
        #endif
    }
    size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
    
    if (fd == -1) {
        if (offset != 0)
            return -EINVAL;

        return (long)vmalloc(this_proc->vma, this_proc->pm, (flags & MAP_FIXED) ? (uintptr_t)addr : 0, 0, pages, mmu_flags);
    }
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->ops || !file->node->ops->mmap)
        return -EINVAL;
    return file->node->ops->mmap(file->node, addr, pages, mmu_flags, flags, offset);
}

long sys_munmap(void *addr, size_t length) {
    if (!addr || !length)
        return -EINVAL;
    
    size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
    vfree(this_proc->vma, this_proc->pm, addr, pages);
    return 0;
}

extern void arch_set_tls(uint64_t base);

long sys_set_tls(uint64_t base) {
    arch_set_tls(base);
    return 0;
}

long sys_mprotect(void *addr, size_t length, int prot) {
    uint64_t mmu_flags = 0;
    if (prot != PROT_NONE) {
        #ifdef __x86_64__
        mmu_flags = PTE_USER;
        if (prot & PROT_READ) mmu_flags |= PTE_PRESENT;
        if (prot & PROT_WRITE) mmu_flags |= PTE_WRITABLE;
        if (!(prot & PROT_EXEC)) mmu_flags |= PTE_NX;
        #elif __aarch64__
        mmu_flags = PTE_VALID | PTE_AF | (prot & PROT_WRITE ? PTE_USER_RW : PTE_USER_RO);
        if (!(prot & PROT_EXEC)) mmu_flags |= PTE_UXN;
        #endif
    }
    size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;

    vprotect(this_proc->vma, this_proc->pm, addr, pages, mmu_flags);
    return 0;
}

long sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    if (sig < 1 || sig >= _NSIG || sig == SIGKILL || sig == SIGSTOP)
        return -EINVAL;

    struct process *proc = this_proc;
    if (!proc)
        return -ESRCH;

    if (oldact && copy_to_user(oldact, &proc->sighand[sig], sizeof(*oldact)) != 0)
        return -EFAULT;

    if (act) {
        struct sigaction new_act;
        if (copy_from_user(&new_act, act, sizeof(new_act)) != 0)
            return -EFAULT;

        if (new_act.sa_handler != SIG_DFL && 
            new_act.sa_handler != SIG_IGN) {

            if (check_user_address(new_act.sa_handler) < 0)
                return -EFAULT;
        }

        proc->sighand[sig] = new_act;
    }

    return 0;
}

long sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    struct process *proc = this_proc;
    if (!proc)
        return -ESRCH;

    if (oldset && copy_to_user(oldset, &proc->blocked, sizeof(*oldset)) != 0)
        return -EFAULT;

    if (set) {
        sigset_t new_set;
        if (copy_from_user(&new_set, set, sizeof(new_set)) != 0)
            return -EFAULT;

        sigdelset(&new_set, SIGKILL);
        sigdelset(&new_set, SIGSTOP);
        
        switch (how) {
            case SIG_BLOCK:
                for (unsigned long i = 0; i < _NSIG_WORDS; i++)
                    proc->blocked.sig[i] |= new_set.sig[i];
                break;
            case SIG_UNBLOCK:
                for (unsigned long i = 0; i < _NSIG_WORDS; i++)
                    proc->blocked.sig[i] &= ~new_set.sig[i];
                break;
            case SIG_SETMASK:
                proc->blocked = new_set;
                break;
            default:
                return -EINVAL;
        }
    }
    
    return 0;
}

extern long arch_restore_signal_context(struct thread *tcb, struct sigframe *frame);

long sys_sigreturn(void) {
    if (!this->sigframe)
        return -EINVAL;
    
    struct sigframe *frame = this->sigframe;
    this->parent->blocked = frame->oldmask;
    this->sigframe = NULL;

    return arch_restore_signal_context(this, frame);
}

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

char hostname[65] = "(none)";

long sys_uname(struct utsname *utsname) {
    if (!utsname)
        return -EFAULT;

    struct utsname buf;
    strncpy(buf.sysname, __kernel_name, sizeof buf.sysname);
    strncpy(buf.nodename, hostname, sizeof buf.nodename);
    snprintf(buf.release, sizeof buf.release, "%d.%d.%d", __kernel_version_major, __kernel_version_minor, __kernel_version_patch);
    snprintf(buf.version, sizeof buf.version, "%s %s %s", __kernel_commit_hash, __kernel_build_date, __kernel_build_time);
    snprintf(buf.machine, sizeof buf.machine, "%s", __kernel_arch);
    return copy_to_user(utsname, &buf, sizeof buf);
}

long sys_getcwd(char *buf, size_t bufsiz) {
    char *path = vfs_resolve_path(this_proc->cwd);
    size_t len = strlen(path) + 1;
    if (bufsiz < len)
        return -ENAMETOOLONG;
    if (copy_to_user(buf, path, len) < 0)
        return -EFAULT;
    kfree(path);
    return 0;
}

long sys_chdir(const char *pathname) {
    COPY_USER_STRING(path, pathname, MAX_PATH);
    vfs_node_t *dir = vfs_open(this_proc->cwd, path, 0);
    kfree(path);
    if (!dir)
        return -ENOENT;
    this_proc->cwd = dir;
    return 0;
}

long sys_pipe(int *pipefd, int flags) {
    int fds[2];
    if (copy_from_user(fds, pipefd, 2 * sizeof(int)) < 0)
        return -EFAULT;
    unixpipe_new(fds, flags);
    if (copy_to_user(pipefd, fds, 2 * sizeof(int)) < 0)
        return -EFAULT;
    return 0;
}

struct pollfd {
	int fd;
	short events;
	short revents;
};

long sys_ppoll(struct pollfd *fds, int nfds, const struct timespec *timeout, const sigset_t *sigmask, size_t sigmask_size) {
    (void)sigmask;
    (void)sigmask_size;
    // TODO: apply the sigmask

    struct timespec to;
    if (timeout && copy_from_user(&to, timeout, sizeof to) < 0)
        return -EFAULT;

    long ready = 0;
    for (int fd = 0; fd < nfds; fd++) {
        struct pollfd *pfd = &fds[fd];
        pfd->revents = 0;

        struct file *file = file_get(pfd->fd);
        if (!file) {
            pfd->revents = POLLNVAL;
            continue;
        }

        if ((pfd->revents = vfs_poll(file->node, pfd->events, 0)))
            ready++;
    }
    if (ready || (timeout && !to.tv_sec && !to.tv_nsec))
        return ready;

    vfs_node_t **nodes = kmalloc(nfds * sizeof(vfs_node_t *));
    short *events = kmalloc(nfds * sizeof(short));
    short *revents = kmalloc(nfds * sizeof(short));
    for (int fd = 0; fd < nfds; fd++) {
        struct pollfd *pfd = &fds[fd];
        pfd->revents = 0;

        struct file *file = file_get(pfd->fd);
        if (!file) {
            pfd->revents = POLLNVAL;
            continue;
        }

        nodes[fd] = file->node;
        events[fd] = pfd->events;
    }

    ready = vfs_poll_multiplexed(nodes, events, revents, nfds, timeout ? to.tv_sec * 1000000000 + to.tv_nsec : -1);
    if (ready < 0) {
        kfree(nodes);
        kfree(events);
        kfree(revents);
        return ready;
    }

    for (int fd = 0; fd < nfds; fd++) {
        struct pollfd *pfd = &fds[fd];
    
        struct file *file = file_get(pfd->fd);
        if (!file) {
            pfd->revents = POLLNVAL;
            continue;
        }

        pfd->revents = revents[fd];
    }

    kfree(nodes);
    kfree(events);
    kfree(revents);
    return ready;
}

long sys_sleep(struct timespec *ts) {
    struct timespec tv;
    if (copy_from_user(&tv, ts, sizeof tv) < 0)
        return -EFAULT;

    sched_sleep(tv.tv_sec * 1000000000UL + tv.tv_nsec);
    return 0;
}

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

long sys_gettime(int clock, struct timespec *ts) {
    struct timespec tv;
    switch (clock) {
        case CLOCK_REALTIME:
            tv.tv_sec = now();
            break;
        case CLOCK_MONOTONIC:
            uptime((size_t *)&tv.tv_sec, (size_t *)&tv.tv_nsec);
            break;
    }
    return copy_to_user(ts, &tv, sizeof tv);
}

long sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, pathname, MAX_PATH);
    vfs_node_t *node = vfs_lookup(dir, path, (flags & AT_SYMLINK_NOFOLLOW) ? false : true, VFS_NONE);
    kfree(path);
    if (!node)
        return -ENOENT;

    if (mode == F_OK)
        return 0;
    if (mode & R_OK && !(node->perms & (S_IRUSR | S_IRGRP | S_IROTH)))
        return -EACCES;
    if (mode & W_OK && !(node->perms & (S_IWUSR | S_IWGRP | S_IWOTH)))
        return -EACCES;
    if (mode & X_OK && !(node->perms & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return -EACCES;
    return 0;
}

long sys_unlinkat(int dirfd, const char *pathname, int flags) {
    (void)flags; // TODO: handle AT_REMOVEDIR

    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, pathname, MAX_PATH);
    vfs_node_t *node = vfs_lookup(dir, path, false, VFS_NONE);
    kfree(path);
    if (!node)
        return -ENOENT;

    return vfs_remove(node);
}

long sys_mkdirat(int dirfd, const char *pathname, unsigned int mode) {
    (void)mode;
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, pathname, MAX_PATH);
    vfs_node_t *node = vfs_lookup(dir, path, true, VFS_DIRECTORY);
    kfree(path);
    if (!node)
        return -EPERM;

    node->perms = mode & ~this_proc->umask;
    return 0;
}

long sys_sethostname(char *name, size_t len) {
    if (len >= sizeof hostname)
        return -EINVAL;
    memset(hostname, 0, sizeof hostname);
    return copy_from_user(hostname, name, len);
}

long sys_socket(int domain, int type, int protocol) {
    return socket_new(domain, type, protocol);
}

long sys_bind(int fd, const void *addr, uint32_t addrlen) {
    return socket_bind(fd, addr, addrlen);
}

long sys_listen(int fd, int backlog) {
    return socket_listen(fd, backlog);
}

long sys_connect(int fd, const void *addr, uint32_t addrlen) {
    return socket_connect(fd, addr, addrlen);
}

long sys_accept(int fd, const void *addr, uint32_t *addrlen) {
    return socket_accept(fd, addr, addrlen);
}

long sys_recvfrom(int fd, void *buffer, size_t size, int flags, const void *addr, socklen_t addrlen) {
    (void)flags;
    (void)addr;
    (void)addrlen;
    return sys_read_write(fd, buffer, size, false, true);
}

long sys_sendto(int fd, const void *buffer, size_t size, int flags, const void *addr, socklen_t addrlen) {
    (void)flags;
    (void)addr;
    (void)addrlen;
    return sys_read_write(fd, (void *)buffer, size, true, false);
}

long sys_fchdir(int fd) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (file->node->type != VFS_DIRECTORY)
        return -ENOTDIR;
    this_proc->cwd = file->node;
    return 0;
}

long sys_renameat(int olddirfd, const char *oldpathname, int newdirfd, const char *newpathname) {
    vfs_node_t *olddir = this_proc->cwd;
    if (olddirfd != AT_FDCWD) {
        struct file *file = file_get(olddirfd);
        if (!file)
            return -EBADF;
        olddir = file->node;
    }

    vfs_node_t *newdir = this_proc->cwd;
    if (newdirfd != AT_FDCWD) {
        struct file *file = file_get(newdirfd);
        if (!file)
            return -EBADF;
        newdir = file->node;
    }

    COPY_USER_STRING(oldpath, oldpathname, MAX_PATH);
    vfs_node_t *node = vfs_lookup(olddir, oldpath, true, VFS_NONE);
    kfree(oldpath);
    if (!node)
        return -ENOENT;

    COPY_USER_STRING(newpath, newpathname, MAX_PATH);
    
    long ret = vfs_rename(node, newdir, newpath);
    kfree(newpath);
    return ret;
}

long sys_reboot(void) {
    acpi_reboot();
    __builtin_unreachable();
}

long sys_shutdown(void) {
    acpi_shutdown();
    __builtin_unreachable();
}

long sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, pathname, MAX_PATH);
    vfs_node_t *node = vfs_lookup(dir, path, false, VFS_NONE);
    kfree(path);
    if (!node)
        return -ENOENT;
    if (node->type != VFS_SYMLINK)
        return -EINVAL;

    char *name = strdup(node->target);
    size_t n = bufsiz < node->size ? bufsiz : node->size;
    copy_to_user(buf, name, n);

    return n;
}

long sys_symlinkat(const char *target, int dirfd, const char *linkpath) {
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }
    
    COPY_USER_STRING(path, linkpath, MAX_PATH);

    vfs_node_t *node = vfs_lookup(dir, path, true, VFS_NONE);
    if (node) {
        kfree(path);
        return -EEXIST;
    }
    node = vfs_lookup(dir, path, true, VFS_SYMLINK);
    kfree(path);
    if (!node)
        return -EPERM;

    COPY_USER_STRING(ktarget, target, MAX_PATH);
    node->target = strdup(ktarget);
    node->size = strlen(ktarget);
    vfs_write(node, node->target, 0, node->size);
    kfree(ktarget);

    return 0;
}

long sys_mount(const char *path, const char *type, const char *device_path, long flags) {
    COPY_USER_STRING(_path, path, MAX_PATH);
    COPY_USER_STRING(_type, type, MAX_PATH);
    COPY_USER_STRING(_device, device_path, MAX_PATH);

    vfs_node_t *node = vfs_lookup(this_proc->cwd, _path, true, VFS_NONE);
    if (!node) {
        kfree(_path);
        kfree(_type);
        kfree(_device);
        return -ENOENT;
    }
    vfs_node_t *device = vfs_lookup(this_proc->cwd, _device, true, VFS_NONE);

    long ret = vfs_mount(node, _type, device, flags);

    kfree(_path);
    kfree(_type);
    kfree(_device);
    return ret;
}

long sys_umount(const char *path, long flags) {
    COPY_USER_STRING(_path, path, MAX_PATH);
    vfs_node_t *node = vfs_lookup(this_proc->cwd, _path, true, VFS_NONE);
    if (!node) {
        kfree(_path);
        return -ENOENT;
    }

    long ret = vfs_unmount(node, flags);
    kfree(_path);
    return ret;
}

long sys_umask(unsigned int mask) {
    unsigned int old_mask = this_proc->umask;
    this_proc->umask = mask;
    return old_mask;
}

long sys_fchmod(int fd, unsigned int mode) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;

    return vfs_chmod(file->node, mode);
}

long sys_chmodat(int dirfd, const char *filename, unsigned int mode, unsigned int flags) {
    vfs_node_t *dir = this_proc->cwd;
    if (dirfd != AT_FDCWD) {
        struct file *file = file_get(dirfd);
        if (!file)
            return -EBADF;
        dir = file->node;
    }

    COPY_USER_STRING(path, filename, MAX_PATH);
    vfs_node_t *node = vfs_lookup(dir, path, (flags & AT_SYMLINK_NOFOLLOW) ? false : true, VFS_NONE);
    kfree(path);
    if (!node)
        return -ENOENT;

    return vfs_chmod(node, mode);
}

long sys_linkat(int olddirfd, const char *oldpathname, int newdirfd, const char *newpathname, int flags) {
    vfs_node_t *olddir = this_proc->cwd;
    if (olddirfd != AT_FDCWD) {
        struct file *file = file_get(olddirfd);
        if (!file)
            return -EBADF;
        olddir = file->node;
    }

    vfs_node_t *newdir = this_proc->cwd;
    if (newdirfd != AT_FDCWD) {
        struct file *file = file_get(newdirfd);
        if (!file)
            return -EBADF;
        newdir = file->node;
    }

    COPY_USER_STRING(oldpath, oldpathname, MAX_PATH);
    vfs_node_t *old_node = vfs_lookup(olddir, oldpath, flags & AT_SYMLINK_FOLLOW, VFS_NONE);
    kfree(oldpath);
    if (!old_node)
        return -ENOENT;
    if (old_node->type == VFS_SYMLINK)
        return -EINVAL;

    COPY_USER_STRING(newpath, newpathname, MAX_PATH);
    vfs_node_t *new_node = vfs_lookup( newdir, newpath, flags & AT_SYMLINK_FOLLOW, old_node->type);
    kfree(newpath);
    if (!new_node)
        return -ENOENT;

    return vfs_link(old_node, new_node);
}

long sys_clone(void *entry, void *stack) {
    struct thread *tcb = sched_new_thread(this_proc, entry, 0, NULL, NULL, NULL, 0, stack);
    list_insert(sched_find_cpu()->threads, tcb);
    return tcb->tid;
}

long sys_exit_thread(void) {
    sched_exit(this, 0);
    __builtin_unreachable();
}

long sys_futex_wait(int *pointer, int expected, const struct timespec *time) {
    return futex_wait(pointer, expected, time);
}

long sys_futex_wake(int *pointer) {
    return futex_wake(pointer);
}

long sys_getsockopt(int fd, int level, int optname, char *optval, uint32_t *optlen) {
    return socket_getsockopt(fd, level, optname, optval, optlen);
}

typedef long (*syscall_func)(long, long, long, long, long, long);

syscall_func syscalls[] = {
    [SYS_read]        = (syscall_func)(uintptr_t)sys_read,
    [SYS_write]       = (syscall_func)(uintptr_t)sys_write,
    [SYS_seek]        = (syscall_func)(uintptr_t)sys_seek,
    [SYS_openat]      = (syscall_func)(uintptr_t)sys_openat,
    [SYS_close]       = (syscall_func)(uintptr_t)sys_close,
    [SYS_fstatat]     = (syscall_func)(uintptr_t)sys_fstatat,
    [SYS_ioctl]       = (syscall_func)(uintptr_t)sys_ioctl,
    [SYS_dup]         = (syscall_func)(uintptr_t)sys_dup,
    [SYS_fcntl]       = (syscall_func)(uintptr_t)sys_fcntl,
    [SYS_readdir]     = (syscall_func)(uintptr_t)sys_readdir,

    [SYS_exit]        = (syscall_func)(uintptr_t)sys_exit,
    [SYS_waitpid]     = (syscall_func)(uintptr_t)sys_waitpid,
    [SYS_kill]        = (syscall_func)(uintptr_t)sys_kill,
    [SYS_fork]        = (syscall_func)(uintptr_t)sys_fork,
    [SYS_exec]        = (syscall_func)(uintptr_t)sys_exec,
    [SYS_getpid]      = (syscall_func)(uintptr_t)sys_getpid,
    [SYS_gettid]      = (syscall_func)(uintptr_t)sys_gettid,
    [SYS_getppid]     = (syscall_func)(uintptr_t)sys_getppid,
    [SYS_getpgid]     = (syscall_func)(uintptr_t)sys_getpgid,
    [SYS_setpgid]     = (syscall_func)(uintptr_t)sys_setpgid,

    [SYS_mmap]        = (syscall_func)(uintptr_t)sys_mmap,
    [SYS_munmap]      = (syscall_func)(uintptr_t)sys_munmap,
    [SYS_set_tls]     = (syscall_func)(uintptr_t)sys_set_tls,
    [SYS_mprotect]    = (syscall_func)(uintptr_t)sys_mprotect,

    [SYS_sigaction]   = (syscall_func)(uintptr_t)sys_sigaction,
    [SYS_sigreturn]   = (syscall_func)(uintptr_t)sys_sigreturn,
    [SYS_sigprocmask] = (syscall_func)(uintptr_t)sys_sigprocmask,

    [SYS_uname]       = (syscall_func)(uintptr_t)sys_uname,
    [SYS_getcwd]      = (syscall_func)(uintptr_t)sys_getcwd,
    [SYS_chdir]       = (syscall_func)(uintptr_t)sys_chdir,
    [SYS_pipe]        = (syscall_func)(uintptr_t)sys_pipe,
    [SYS_ppoll]       = (syscall_func)(uintptr_t)sys_ppoll,
    [SYS_sleep]       = (syscall_func)(uintptr_t)sys_sleep,
    [SYS_gettime]     = (syscall_func)(uintptr_t)sys_gettime,
    [SYS_faccessat]   = (syscall_func)(uintptr_t)sys_faccessat,
    [SYS_unlinkat]    = (syscall_func)(uintptr_t)sys_unlinkat,
    [SYS_mkdirat]     = (syscall_func)(uintptr_t)sys_mkdirat,
    [SYS_sethostname] = (syscall_func)(uintptr_t)sys_sethostname,

    [SYS_socket]      = (syscall_func)(uintptr_t)sys_socket,
    [SYS_bind]        = (syscall_func)(uintptr_t)sys_bind,
    [SYS_listen]      = (syscall_func)(uintptr_t)sys_listen,
    [SYS_connect]     = (syscall_func)(uintptr_t)sys_connect,
    [SYS_accept]      = (syscall_func)(uintptr_t)sys_accept,
    [SYS_recvfrom]    = (syscall_func)(uintptr_t)sys_recvfrom,
    [SYS_sendto]      = (syscall_func)(uintptr_t)sys_sendto,

    [SYS_reboot]      = (syscall_func)(uintptr_t)sys_reboot,
    [SYS_shutdown]    = (syscall_func)(uintptr_t)sys_shutdown,

    [SYS_fchdir]      = (syscall_func)(uintptr_t)sys_fchdir,
    [SYS_renameat]    = (syscall_func)(uintptr_t)sys_renameat,
    [SYS_readlinkat]  = (syscall_func)(uintptr_t)sys_readlinkat,
    [SYS_symlinkat]   = (syscall_func)(uintptr_t)sys_symlinkat,
    [SYS_mount]       = (syscall_func)(uintptr_t)sys_mount,
    [SYS_umount]      = (syscall_func)(uintptr_t)sys_umount,
    [SYS_umask]       = (syscall_func)(uintptr_t)sys_umask,
    [SYS_fchmod]      = (syscall_func)(uintptr_t)sys_fchmod,
    [SYS_chmodat]     = (syscall_func)(uintptr_t)sys_chmodat,
    [SYS_linkat]      = (syscall_func)(uintptr_t)sys_linkat,

    [SYS_clone]       = (syscall_func)(uintptr_t)sys_clone,
    [SYS_exit_thread] = (syscall_func)(uintptr_t)sys_exit_thread,
    [SYS_futex_wait]  = (syscall_func)(uintptr_t)sys_futex_wait,
    [SYS_futex_wake]  = (syscall_func)(uintptr_t)sys_futex_wake,
    [SYS_getsockopt]  = (syscall_func)(uintptr_t)sys_getsockopt
};

long syscall_handler(size_t *args) {
    if (args[0] >= sizeof syscalls / sizeof(void *) || !syscalls[args[0]]) {
        dprintf(LOG_INFO, "\033[93muser:\033[0m unknown syscall %lu\n", args[0]);
        return -ENOSYS;
    }

    syscall_func handler = syscalls[args[0]];
    return handler(args[1], args[2], args[3], args[4], args[5], args[6]);
}