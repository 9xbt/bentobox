#include <kernel/errno.h>
#include <kernel/vfs.h>
#include <kernel/tmpfs.h>
#include <kernel/malloc.h>
#include <kernel/string.h>

/**
 * TODO: make use of lists
 **/

struct tmpfs_file_data {
    void *data;
    size_t allocated, used;
    struct vfs_node *node;
    struct tmpfs_file_data *next;
};

static struct tmpfs_file_data *tmpfs_files[TMPFS_HASH_SIZE] = {0};

static struct tmpfs_file_data *tmpfs_find_file_data(struct vfs_node *node) {
    struct tmpfs_file_data *file_data = tmpfs_files[node->inode % TMPFS_HASH_SIZE];
    while (file_data && file_data->node != node)
        file_data = file_data->next;
    return file_data;
}

static struct tmpfs_file_data *tmpfs_create_file_data(struct vfs_node *node) {
    struct tmpfs_file_data *file_data = kmalloc(sizeof(struct tmpfs_file_data));
    if (!file_data) return NULL;
    
    file_data->node = node;
    
    size_t hash = node->inode % TMPFS_HASH_SIZE;
    file_data->next = tmpfs_files[hash];
    tmpfs_files[hash] = file_data;
    return file_data;
}

static void tmpfs_remove_file_data(struct vfs_node *node) {
    struct tmpfs_file_data **ptr = &tmpfs_files[node->inode % TMPFS_HASH_SIZE];
    
    while (*ptr) {
        if ((*ptr)->node == node) {
            struct tmpfs_file_data *to_remove = *ptr;
            *ptr = (*ptr)->next;
            kfree(to_remove->data);
            kfree(to_remove);
            return;
        }
        ptr = &(*ptr)->next;
    }
}

static int tmpfs_resize(struct tmpfs_file_data *file_data, size_t new_size) {
    if (new_size <= file_data->allocated) {
        file_data->used = file_data->node->size = new_size;
        return 0;
    }
    
    size_t new_allocated = (new_size + 4095) & ~4095;
    void *new_data = kmalloc(new_allocated);
    if (!new_data) return -ENOMEM;
    
    if (file_data->data) {
        if (file_data->used) memcpy(new_data, file_data->data, file_data->used);
        kfree(file_data->data);
    }
    
    file_data->data = new_data;
    file_data->allocated = new_allocated;
    file_data->used = file_data->node->size = new_size;
    return 0;
}

int tmpfs_truncate(struct vfs_node *node, size_t new_size) {
    if (!node || node->type != VFS_FILE) return -EINVAL;
    
    struct tmpfs_file_data *file_data = tmpfs_find_file_data(node);
    if (!file_data) {
        if (!new_size) return 0;
        return (file_data = tmpfs_create_file_data(node)) ? 0 : -ENOMEM;
    }
    
    if (!new_size) {
        kfree(file_data->data);
        file_data->data = NULL;
        file_data->allocated = file_data->used = node->size = 0;
        return 0;
    }
    
    if (new_size < file_data->used) {
        file_data->used = node->size = new_size;
        return 0;
    }
    return tmpfs_resize(file_data, new_size);
}

long tmpfs_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node || !buffer || offset < 0) return -EINVAL;
    if (node->type != VFS_FILE) return -EISDIR;
    
    struct tmpfs_file_data *file_data = tmpfs_find_file_data(node);
    if (!file_data || (size_t)offset >= file_data->used) return file_data ? 0 : -ENOENT;
    
    size_t bytes_to_read = (offset + len > file_data->used) ? 
                          file_data->used - offset : len;
    if (!bytes_to_read) return 0;
    
    memcpy(buffer, (char*)file_data->data + offset, bytes_to_read);
    return bytes_to_read;
}

long tmpfs_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node || !buffer || offset < 0) return -EINVAL;
    if (node->type != VFS_FILE) return -EISDIR;
    
    struct tmpfs_file_data *file_data = tmpfs_find_file_data(node);
    if (!file_data && !(file_data = tmpfs_create_file_data(node))) return -ENOMEM;
    
    if (!offset) {
        int ret = tmpfs_truncate(node, 0);
        if (ret) return ret;
    }
    
    size_t new_size = offset + len;
    if (new_size > file_data->used) {
        int ret = tmpfs_resize(file_data, new_size);
        if (ret) return ret;
    }
    
    memcpy((char*)file_data->data + offset, buffer, len);
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
    
    if (!tmpfs_create_file_data(file)) {
        kfree(file);
        return NULL;
    }
    
    vfs_add_node(parent, file);
    return file;
}

long tmpfs_remove_file(struct vfs_node *node) {
    if (!node || node->type != VFS_FILE) return -EINVAL;
    tmpfs_remove_file_data(node);
    return 0;
}

void tmpfs_initialize(void) {
    struct vfs_node *tmp = vfs_create_node("tmp", VFS_DIRECTORY);
    tmp->inode = TMPFS_ROOT;
    tmp->driver = VFS_DRIVER_TMPFS;
    vfs_add_node(NULL, tmp);
}