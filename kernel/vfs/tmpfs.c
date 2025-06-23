#include <kernel/errno.h>
#include <kernel/vfs.h>
#include <kernel/tmpfs.h>
#include <kernel/malloc.h>
#include <kernel/string.h>

struct tmpfs_file_data {
    void *data;
    size_t allocated;
    size_t used;
    struct vfs_node *node;
    struct tmpfs_file_data *next;
};

static struct tmpfs_file_data *tmpfs_files[TMPFS_HASH_SIZE] = {0};

static size_t tmpfs_hash(uint64_t inode) {
    return inode % TMPFS_HASH_SIZE;
}

static struct tmpfs_file_data *tmpfs_create_file_data(struct vfs_node *node) {
    struct tmpfs_file_data *file_data = kmalloc(sizeof(struct tmpfs_file_data));
    if (!file_data)
        return NULL;
    
    file_data->data = NULL;
    file_data->allocated = 0;
    file_data->used = 0;
    file_data->node = node;
    
    unsigned int hash = tmpfs_hash(node->inode);
    file_data->next = tmpfs_files[hash];
    tmpfs_files[hash] = file_data;
    return file_data;
}

static struct tmpfs_file_data *tmpfs_find_file_data(struct vfs_node *node) {
    unsigned int hash = tmpfs_hash(node->inode);
    struct tmpfs_file_data *file_data = tmpfs_files[hash];
    
    while (file_data) {
        if (file_data->node == node) {
            return file_data;
        }
        file_data = file_data->next;
    }
    return NULL;
}

static void tmpfs_remove_file_data(struct vfs_node *node) {
    unsigned int hash = tmpfs_hash(node->inode);
    struct tmpfs_file_data **file_data_ptr = &tmpfs_files[hash];
    
    while (*file_data_ptr) {
        if ((*file_data_ptr)->node == node) {
            struct tmpfs_file_data *to_remove = *file_data_ptr;
            *file_data_ptr = (*file_data_ptr)->next;
            
            if (to_remove->data) {
                kfree(to_remove->data);
            }
            kfree(to_remove);
            return;
        }
        file_data_ptr = &(*file_data_ptr)->next;
    }
}

static int tmpfs_resize(struct tmpfs_file_data *file_data, size_t new_size) {
    if (new_size <= file_data->allocated) {
        file_data->used = new_size;
        file_data->node->size = new_size;
        return 0;
    }
    
    size_t new_allocated = (new_size + 4095) & ~4095; /* TODO: this should be 4096-sizeof(heap_block) */
    void *new_data = kmalloc(new_allocated);
    if (!new_data) return -ENOMEM;
    
    if (file_data->data && file_data->used > 0) {
        memcpy(new_data, file_data->data, file_data->used);
        kfree(file_data->data);
    }
    
    file_data->data = new_data;
    file_data->allocated = new_allocated;
    file_data->used = new_size;
    file_data->node->size = new_size;
    return 0;
}

int tmpfs_truncate(struct vfs_node *node, size_t new_size) {
    if (!node || node->type != VFS_FILE)
        return -EINVAL;
    
    struct tmpfs_file_data *file_data = tmpfs_find_file_data(node);
    if (!file_data) {
        if (new_size == 0)
            return 0;
        file_data = tmpfs_create_file_data(node);
        if (!file_data)
            return -ENOMEM;
    }
    
    if (new_size == 0) {
        if (file_data->data) {
            kfree(file_data->data);
            file_data->data = NULL;
        }
        file_data->allocated = 0;
        file_data->used = 0;
        node->size = 0;
        return 0;
    }
    
    if (new_size < file_data->used) {
        file_data->used = new_size;
        node->size = new_size;
        return 0;
    }
    return tmpfs_resize(file_data, new_size);
}

long tmpfs_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node || !buffer)
        return -EINVAL;
    if (node->type != VFS_FILE)
        return -EISDIR;
    if (offset < 0)
        return -EINVAL;
    
    struct tmpfs_file_data *file_data = tmpfs_find_file_data(node);
    if (!file_data)
        return -ENOENT;
    if ((size_t)offset >= file_data->used)
        return 0;
    
    size_t bytes_to_read = len;
    if (offset + bytes_to_read > file_data->used) {
        bytes_to_read = file_data->used - offset;
    }
    
    if (bytes_to_read == 0)
        return 0;
    memcpy(buffer, (char*)file_data->data + offset, bytes_to_read);
    return bytes_to_read;
}

long tmpfs_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (!node || !buffer)
        return -EINVAL;
    if (node->type != VFS_FILE)
        return -EISDIR;
    if (offset < 0)
        return -EINVAL;
    
    struct tmpfs_file_data *file_data = tmpfs_find_file_data(node);
    if (!file_data) {
        file_data = tmpfs_create_file_data(node);
        if (!file_data) return -ENOMEM;
    }
    
    if (offset == 0) {
        int ret = tmpfs_truncate(node, 0);
        if (ret != 0) return ret;
    }
    
    size_t new_size = offset + len;
    if (new_size > file_data->used) {
        int ret = tmpfs_resize(file_data, new_size);
        if (ret != 0) return ret;
    }
    
    memcpy((char*)file_data->data + offset, buffer, len);
    return len;
}

struct vfs_node *tmpfs_create_file(struct vfs_node *parent, const char *name) {
    if (!parent || parent->type != VFS_DIRECTORY)
        return NULL;
    struct vfs_node *file = vfs_create_node(name, VFS_FILE);
    if (!file)
        return NULL;
    
    file->read = tmpfs_read;
    file->write = tmpfs_write;
    file->driver = VFS_DRIVER_TMPFS;
    
    static uint64_t next_inode = 1000000;
    file->inode = next_inode++;
    
    struct tmpfs_file_data *file_data = tmpfs_create_file_data(file);
    if (!file_data) {
        kfree(file);
        return NULL;
    }
    
    vfs_add_node(parent, file);
    return file;
}

int tmpfs_remove_file(struct vfs_node *node) {
    if (!node || node->type != VFS_FILE)
        return -EINVAL;
    
    tmpfs_remove_file_data(node);
    return 0;
}

void tmpfs_initialize(void) {
    struct vfs_node *tmp = vfs_create_node("tmp", VFS_DIRECTORY);
    tmp->inode = TMPFS_ROOT;
    tmp->driver = VFS_DRIVER_TMPFS;
    vfs_add_node(NULL, tmp);
}