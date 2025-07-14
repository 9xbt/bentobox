#include <errno.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

#define TMPFS_ROOT 999999
#define TMPFS_HASH_SIZE 256

/**
 * TODO: make use of lists
 **/

struct tmpfs_fd {
    void *data;
    size_t allocated, used;
    struct vfs_node *node;
    struct tmpfs_fd *next;
};

static struct tmpfs_fd *tmpfs_files[TMPFS_HASH_SIZE] = {0};

static struct tmpfs_fd *tmpfs_find_fd(struct vfs_node *node) {
    struct tmpfs_fd *fd = tmpfs_files[node->inode % TMPFS_HASH_SIZE];
    while (fd && fd->node != node)
        fd = fd->next;
    return fd;
}

static struct tmpfs_fd *tmpfs_create_fd(struct vfs_node *node) {
    struct tmpfs_fd *fd = kmalloc(sizeof(struct tmpfs_fd));
    if (!fd) return NULL;
    
    fd->data = NULL;
    fd->allocated = 0;
    fd->used = 0;
    fd->node = node;
    
    size_t hash = node->inode % TMPFS_HASH_SIZE;
    fd->next = tmpfs_files[hash];
    tmpfs_files[hash] = fd;
    return fd;
}

static void tmpfs_remove_fd(struct vfs_node *node) {
    struct tmpfs_fd **ptr = &tmpfs_files[node->inode % TMPFS_HASH_SIZE];
    
    while (*ptr) {
        if ((*ptr)->node == node) {
            struct tmpfs_fd *fd = *ptr;
            *ptr = (*ptr)->next;
            if (fd->data) kfree(fd->data);
            kfree(fd);
            return;
        }
        ptr = &(*ptr)->next;
    }
}

static int tmpfs_resize(struct tmpfs_fd *fd, size_t new_size) {
    if (new_size <= fd->allocated) {
        fd->used = fd->node->size = new_size;
        return 0;
    }
    
    size_t new_allocated = (new_size + 4095) & ~4095;
    void *new_data = kmalloc(new_allocated);
    if (!new_data) return -ENOMEM;
    
    if (fd->data) {
        if (fd->used) memcpy(new_data, fd->data, fd->used);
        kfree(fd->data);
    }
    
    fd->data = new_data;
    fd->allocated = new_allocated;
    fd->used = fd->node->size = new_size;
    return 0;
}

int tmpfs_truncate(struct vfs_node *node, size_t new_size) {
    if (!node || node->type != VFS_FILE) return -EINVAL;
    
    struct tmpfs_fd *fd = tmpfs_find_fd(node);
    if (!fd) {
        if (!new_size) return 0;
        return (fd = tmpfs_create_fd(node)) ? 0 : -ENOMEM;
    }
    
    if (!new_size) {
        kfree(fd->data);
        fd->data = NULL;
        fd->allocated = fd->used = node->size = 0;
        return 0;
    }
    
    if (new_size < fd->used) {
        fd->used = node->size = new_size;
        return 0;
    }
    return tmpfs_resize(fd, new_size);
}

long tmpfs_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node || !buffer || offset < 0) return -EINVAL;
    if (node->type != VFS_FILE) return -EISDIR;
    
    struct tmpfs_fd *fd = tmpfs_find_fd(node);
    if (!fd || (size_t)offset >= fd->used) return fd ? 0 : -ENOENT;
    
    size_t bytes_to_read = (offset + len > fd->used) ? 
                          fd->used - offset : len;
    if (!bytes_to_read) return 0;

    memcpy(buffer, (char*)fd->data + offset, bytes_to_read);
    return bytes_to_read;
}

long tmpfs_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node || !buffer || offset < 0) return -EINVAL;
    if (node->type != VFS_FILE) return -EISDIR;
    
    struct tmpfs_fd *fd = tmpfs_find_fd(node);
    if (!fd && !(fd = tmpfs_create_fd(node))) return -ENOMEM;
    
    if (!offset) {
        int ret = tmpfs_truncate(node, 0);
        if (ret) return ret;
    }
    
    size_t new_size = offset + len;
    if (new_size > fd->used) {
        int ret = tmpfs_resize(fd, new_size);
        if (ret) return ret;
    }
    
    memcpy((char*)fd->data + offset, buffer, len);
    return len;
}

struct vfs_node *tmpfs_create_file(struct vfs_node *parent, const char *name) {
    if (!parent || parent->type != VFS_DIRECTORY) return NULL;
    
    struct vfs_node *file = vfs_create_node(name, VFS_FILE);
    if (!file) return NULL;
    
    static uint64_t next_inode = 1000000;
    file->inode = next_inode++;
    file->read = tmpfs_read;
    file->write = tmpfs_write;
    file->driver = VFS_DRIVER_TMPFS;
    
    if (!tmpfs_create_fd(file)) {
        kfree(file);
        return NULL;
    }
    
    vfs_add_node(parent, file);
    return file;
}

long tmpfs_remove_file(struct vfs_node *node) {
    if (!node || node->type != VFS_FILE) return -EINVAL;
    tmpfs_remove_fd(node);
    return 0;
}

void tmpfs_initialize(void) {
    struct vfs_node *tmp = vfs_create_node("tmp", VFS_DIRECTORY);
    tmp->inode = TMPFS_ROOT;
    tmp->driver = VFS_DRIVER_TMPFS;
    vfs_add_node(NULL, tmp);
}