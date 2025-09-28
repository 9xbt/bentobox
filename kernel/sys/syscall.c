#include <stddef.h>
#include <stdint.h>
#include <kernel/syscall.h>
#include <kernel/version.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/malloc.h>
#include <kernel/errno.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/mmu.h>

static long sys_read_write(int fd, void *buf, size_t len, bool write) {
    struct file *file = file_get(fd);
    if (!file || !file->open)
        return -EBADFD;
    if (!file->node)
        return -ENOENT;
    if (!file->node->ops || (!file->node->ops->write && write) || (!file->node->ops->read && !write))
        return 0;

    void *buffer = kmalloc(len);
    if (write) copy_from_user(buffer, buf, len);

    long ret = write ?
        vfs_write(file->node, buffer, file->offset, len) :
        vfs_read(file->node, buffer, file->offset, len);
    file->offset += ret;

    if (!write) copy_to_user(buf, buffer, ret);
    kfree(buffer);
    return ret;
}

long sys_read(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, false);
}

long sys_write(int fd, void *buffer, size_t len) {
    return sys_read_write(fd, buffer, len, true);
}

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
    }

    return file->offset;
}

long sys_open(const char *pathname, int flags) {
    COPY_USER_STRING(path, pathname, MAX_PATH);
    long fd = file_open(path, flags);
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
    if (!node)
        return -ENOENT;

    memset(statbuf, 0, sizeof(struct stat));
    switch (node->type) {
        case VFS_FILE:
            statbuf->st_mode |= S_IFREG;
            break;
        case VFS_DIRECTORY:
            statbuf->st_mode |= S_IFDIR;
            break;
        case VFS_CHARDEVICE:
            statbuf->st_mode |= S_IFCHR;
            break;
        case VFS_BLOCKDEVICE:
            statbuf->st_mode |= S_IFBLK;
            break;
        case VFS_SYMLINK:
            statbuf->st_mode |= S_IFLNK;
            break;
        default:
            statbuf->st_mode |= S_IFREG;
            break;
    }
    statbuf->st_mode |= (node->perms & 07777);

    statbuf->st_nlink = 1;
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
    statbuf->st_ino = node->inode;
    statbuf->st_atim.tv_sec = node->atime;
    statbuf->st_ctim.tv_sec = node->ctime;
    statbuf->st_mtim.tv_sec = node->mtime;
    
    switch (node->type) {
        case VFS_FILE:
        case VFS_DIRECTORY:
            statbuf->st_size = node->size;
            statbuf->st_blocks = node->blocks;
            break;
        case VFS_SYMLINK:
            statbuf->st_size = node->symlink ? ((flags & AT_SYMLINK_NOFOLLOW) ? strlen(node->symlink->name) : node->symlink->size) : 0;
            break;
        default:
            statbuf->st_size = 0;
            break;
    }
    
    kfree(path);
    return 0;
}

long sys_ioctl(int fd, int op, void *arg) {
    struct file *file = file_get(fd);
    if (!file)
        return -EBADF;
    if (!file->node->tty_ops || !file->node->tty_ops->ioctl)
        return -ENOTTY;
    if (check_user_address(arg) < 0)
        return -EFAULT;
    return file->node->tty_ops->ioctl(fd, op, arg);
}

long sys_dup(int oldfd, int newfd, int flags) {
    return file_dup(oldfd, newfd, flags);
}

long sys_exit(int status) {
    (void)status;
    sched_exit(this);
    __builtin_unreachable();
}

long sys_waitpid(int pid, int *wstatus, int options) {
    (void)pid;
    (void)options;

    if (this_proc->children->length == 0)
        return -ECHILD;
    
    foreach_safe(i, this_proc->children) {
        struct process *proc = i->value;
        if (proc->state == PROCESS_ZOMBIE ||
            proc->state == PROCESS_ZOMBIE_ALL)
        {
            *wstatus = 0;
            return 0;
        }
    }

    this->state = THREAD_PAUSED;
    sched_yield();

    *wstatus = 0;
    return 0;
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

long sys_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr;
    if (!length)
        return -EINVAL;
    
    if (fd == -1) {
        if (offset != 0)
            return -EINVAL;

        uint64_t vma_flags = 0;
        if (prot != PROT_NONE) {
            vma_flags = PTE_USER;
            #ifdef __x86_64__
            if (prot & PROT_READ) vma_flags |= PTE_PRESENT;
            if (prot & PROT_WRITE) vma_flags |= PTE_WRITABLE;
            if (!(prot & PROT_EXEC)) vma_flags |= PTE_NX;
            #elif __aarch64__
            if ((prot & PROT_READ) || (prot & PROT_WRITE)) vma_flags |= PTE_VALID | PTE_AF;
            if (prot & PROT_READ) vma_flags |= PTE_RO;
            if (prot & PROT_WRITE) vma_flags |= PTE_RW;
            if (!(prot & PROT_EXEC)) vma_flags |= PTE_UXN;
            #endif
        }

        size_t pages = ALIGN_UP(length, PAGE_SIZE) / PAGE_SIZE;
        void *ptr = vmalloc(this_proc->vma, this_proc->pm, (flags & MAP_FIXED) ? (uintptr_t)addr : 0, pages, vma_flags);

        return (long)ptr;
    }
    return -ENOSYS;
}

extern void arch_set_tls(uint64_t base);

long sys_set_tls(uint64_t base) {
    arch_set_tls(base);
    return 0;
}

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

long sys_uname(struct utsname *utsname) {
    if (!utsname)
        return -EFAULT;

    struct utsname buf;
    strncpy(buf.sysname, __kernel_name, sizeof buf.sysname);
    strncpy(buf.nodename, "localhost", sizeof buf.nodename);
    snprintf(buf.release, sizeof buf.release, "%d.%d.%d", __kernel_version_major, __kernel_version_minor, __kernel_version_patch);
    snprintf(buf.version, sizeof buf.version, "%s %s %s", __kernel_commit_hash, __kernel_build_date, __kernel_build_time);
    snprintf(buf.machine, sizeof buf.machine, "%s", __kernel_arch);
    return copy_to_user(utsname, &buf, sizeof buf);
}

typedef long (*syscall_func)(long, long, long, long, long, long);

syscall_func syscalls[] = {
    [SYS_write]     = (syscall_func)(uintptr_t)sys_write,
    [SYS_read]      = (syscall_func)(uintptr_t)sys_read,
    [SYS_seek]      = (syscall_func)(uintptr_t)sys_seek,
    [SYS_open]      = (syscall_func)(uintptr_t)sys_open,
    [SYS_close]     = (syscall_func)(uintptr_t)sys_close,
    [SYS_fstatat]   = (syscall_func)(uintptr_t)sys_fstatat,
    [SYS_ioctl]     = (syscall_func)(uintptr_t)sys_ioctl,
    [SYS_dup]       = (syscall_func)(uintptr_t)sys_dup,

    [SYS_exit]      = (syscall_func)(uintptr_t)sys_exit,
    [SYS_waitpid]   = (syscall_func)(uintptr_t)sys_waitpid,
    [SYS_fork]      = (syscall_func)(uintptr_t)sys_fork,
    [SYS_exec]      = (syscall_func)(uintptr_t)sys_exec,
    
    [SYS_getpid]    = (syscall_func)(uintptr_t)sys_getpid,
    [SYS_gettid]    = (syscall_func)(uintptr_t)sys_gettid,
    [SYS_getppid]   = (syscall_func)(uintptr_t)sys_getppid,

    [SYS_mmap]      = (syscall_func)(uintptr_t)sys_mmap,
    [SYS_set_tls]   = (syscall_func)(uintptr_t)sys_set_tls,

    [SYS_uname]     = (syscall_func)(uintptr_t)sys_uname
};

long syscall_handler(size_t *args) {
    if (args[0] >= sizeof syscalls / sizeof(void *) || !syscalls[args[0]]) {
        dprintf(LOG_INFO, "\033[93muser:\033[0m unknown syscall %lu\n", args[0]);
        return -ENOSYS;
    }

    syscall_func handler = syscalls[args[0]];
    return handler(args[1], args[2], args[3], args[4], args[5], args[6]);
}