#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/assert.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/errno.h>
#include <kernel/panic.h>
#include <kernel/args.h>
#include <kernel/list.h>
#include <kernel/time.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

#define EXT_FIFO        0x1000
#define EXT_CHAR_DEV    0x2000
#define EXT_DIRECTORY   0x4000
#define EXT_BLOCK_DEV   0x6000
#define EXT_FILE        0x8000
#define EXT_SYM_LINK    0xA000
#define EXT_UNIX_SOCKET 0xC000

typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t su_resv_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t block_num;
    uint32_t log2_block;
    uint32_t log2_frag;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t last_mount_time;
    uint32_t last_write_time;
    uint16_t mount_times_check;
    uint16_t mount_times_allowed;
    uint16_t signature;
    uint16_t state;
    uint16_t err_handle;
    uint16_t minor_ver;
    uint32_t last_consistency_check;
    uint32_t consistency_interval;
    uint32_t os_id;
    uint32_t major_ver;
    uint16_t resv_blocks_user_id;
    uint16_t resv_blocks_group_id;

    uint32_t first_inode;
    uint16_t inode_size;
    uint16_t sb_bgd;
    uint32_t opt_features;
    uint32_t req_features;
    uint32_t mount_features;
    uint8_t  fs_id[16];
    char     vol_name[16];
    char     vol_path_mount[64];
    uint32_t compression_algo;
    uint8_t  preallocate_blocks_file;
    uint8_t  preallocate_blocks_dir;
    uint16_t unused;
    uint64_t journal_id[2];
    uint32_t journal_inode;
    uint32_t journal_device;
    uint32_t orphan_inode_list;
} ext2_sb;

typedef struct {
    uint16_t type_perms;
    uint16_t user_id;
    uint32_t size;
    uint32_t last_access_time;
    uint32_t creation_time;
    uint32_t mod_time;
    uint32_t deletion_time;
    uint16_t group_id;
    uint16_t hard_link_count;
    uint32_t sector_count;
    uint32_t flags;
    uint32_t os_spec;
    uint32_t direct_block_ptr[12];
    uint32_t singly_block_ptr;
    uint32_t doubly_block_ptr;
    uint32_t triply_block_ptr;
    uint32_t gen_number;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t frag_block_addr;
    uint8_t  os_spec2[12];
} ext2_inode;

typedef struct {
    uint32_t inode;
    uint16_t total_size;
    uint8_t  name_len;
    uint8_t  type;
    char     name[];
} ext2_dirent;

typedef struct {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t directories_count;
    uint16_t pad;
    uint8_t  resv[12];
} ext2_bgd;

typedef struct {
    vfs_node_t *device;
    ext2_sb    *sb;
    ext2_bgd   *bgd_table;
    uint32_t block_size;
    uint32_t bgd_count;
    uint32_t bgd_block;
    uint32_t inode_size;
    spinlock_t sb_lock;
    spinlock_t *bg_locks;
} ext2_fs;

#define EXT2_FS_FLAGS_MOUNTED 0x1

enum vfs_node_type ext2_get_type(uint16_t type_perms);

vfs_result_t ext2_open(vfs_node_t *node, int flags);
long ext2_read(vfs_node_t *node, void *buffer, long offset, size_t len);
long ext2_write(vfs_node_t *node, const void *buffer, long offset, size_t len);
vfs_result_t ext2_create(vfs_node_t *parent, const char *name, enum vfs_node_type type);
long ext2_remove(vfs_node_t *node);
long ext2_rename(vfs_node_t *node, vfs_node_t *parent, const char *name);
long ext2_chmod(vfs_node_t *node, unsigned int mode);

vfs_ops_t ext2_ops = {
    .open   = ext2_open,
    .read   = ext2_read,
    .write  = ext2_write,
    .create = ext2_create,
    .remove = ext2_remove,
    .rename = ext2_rename,
    .chmod  = ext2_chmod
};

void ext2_read_block(ext2_fs *fs, uint32_t block, void *buffer, uint32_t count) {
    assert(block);
retry:
    if (vfs_read(fs->device, buffer, block * fs->block_size, count) == -EAGAIN) {
        vfs_poll(fs->device, POLLIN, -1);
        goto retry;
    }
}

void ext2_write_block(ext2_fs *fs, uint32_t block, void *buffer, uint32_t count) {
    assert(block);
retry:
    if (vfs_write(fs->device, buffer, block * fs->block_size, count) == -EAGAIN) {
        vfs_poll(fs->device, POLLOUT, -1);
        goto retry;
    }
}

void ext2_write_bgd(ext2_fs *fs, uint32_t group, ext2_bgd bgd) {
    fs->bgd_table[group] = bgd;
    ext2_write_block(fs, fs->bgd_block, fs->bgd_table, fs->block_size);
}

void ext2_write_sb(ext2_fs *fs) {
    char buf[512];
    memcpy(buf, fs->sb, sizeof(ext2_sb));
retry:
    if (vfs_write(fs->device, buf, 1024, sizeof buf) == -EAGAIN) {
        vfs_poll(fs->device, POLLOUT, -1);
        goto retry;
    }
}

void ext2_read_inode(ext2_fs *fs, uint32_t inode, ext2_inode *in) {
    assert(inode);
    // assert(fs->inode_size == sizeof(ext2_inode));

    inode--;
    uint32_t block_group = inode / fs->sb->inodes_per_group;
    uint32_t inode_index = inode % fs->sb->inodes_per_group;
    uint32_t inode_block = (inode_index * fs->inode_size) / fs->block_size;

    assert(block_group < fs->bgd_count);

    uint8_t buffer[fs->block_size];
    ext2_read_block(fs, fs->bgd_table[block_group].inode_table + inode_block, buffer, fs->block_size);
    // NOTE: we're ignoring the extra bytes of the inode here.
    memcpy(in, buffer + (inode_index % (fs->block_size / fs->inode_size)) * fs->inode_size, sizeof(ext2_inode));
}

void ext2_write_inode(ext2_fs *fs, uint32_t inode, ext2_inode *in) {
    assert(inode);

    inode--;
    uint32_t block_group = inode / fs->sb->inodes_per_group;
    uint32_t inode_index = inode % fs->sb->inodes_per_group;
    uint32_t inode_block = (inode_index * fs->inode_size) / fs->block_size;
    uint32_t inode_offset = (inode_index % (fs->block_size / fs->inode_size)) * fs->inode_size;

    assert(block_group < fs->bgd_count);

    acquire(&fs->bg_locks[block_group]);
    uint8_t buffer[fs->block_size];
    ext2_read_block(fs, fs->bgd_table[block_group].inode_table + inode_block, buffer, fs->block_size);
    // NOTE: we're ignoring the extra bytes of the inode here.
    memcpy(buffer + inode_offset, in, sizeof(ext2_inode));
    ext2_write_block(fs, fs->bgd_table[block_group].inode_table + inode_block, buffer, fs->block_size);
    release(&fs->bg_locks[block_group]);
}

uint32_t ext2_allocate_block(ext2_fs *fs) {
    for (uint32_t group = 0; group < fs->bgd_count; group++) {
        uint8_t *bitmap = kmalloc(fs->block_size);
        acquire(&fs->bg_locks[group]);
        ext2_read_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);
        
        for (uint32_t i = 0; i < fs->sb->blocks_per_group; i++) {
            if (!bitmap_get(bitmap, i)) {
                bitmap_set(bitmap, i);
                ext2_write_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);
                kfree(bitmap);

                acquire(&fs->sb_lock);
                fs->sb->free_blocks_count--;
                ext2_write_sb(fs);
                release(&fs->sb_lock);

                fs->bgd_table[group].free_blocks--;
                ext2_write_bgd(fs, group, fs->bgd_table[group]);
                
                release(&fs->bg_locks[group]);
                return fs->sb->block_num + group * fs->sb->blocks_per_group + i;
            }
        }
        release(&fs->bg_locks[group]);
        kfree(bitmap);
    }
    return 0;
}

uint32_t ext2_allocate_blocks(ext2_fs *fs, uint32_t count, uint32_t *blocks) {
    uint32_t allocated = 0;
    
    for (uint32_t group = 0; group < fs->bgd_count && allocated < count; group++) {
        uint8_t *bitmap = kmalloc(fs->block_size);
        acquire(&fs->bg_locks[group]);
        ext2_read_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);
        
        uint32_t bg_allocated = 0;
        for (uint32_t i = 0; i < fs->sb->blocks_per_group && allocated < count; i++) {
            if (!bitmap_get(bitmap, i)) {
                bitmap_set(bitmap, i);
                blocks[allocated++] = fs->sb->block_num + group * fs->sb->blocks_per_group + i;
                bg_allocated++;
            }
        }
        
        if (bg_allocated > 0) {
            ext2_write_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);
            
            acquire(&fs->sb_lock);
            fs->sb->free_blocks_count -= bg_allocated;
            ext2_write_sb(fs);
            release(&fs->sb_lock);
            
            fs->bgd_table[group].free_blocks -= bg_allocated;
            ext2_write_bgd(fs, group, fs->bgd_table[group]);
        }
        
        release(&fs->bg_locks[group]);
        kfree(bitmap);
    }
    
    return allocated;
}

uint32_t ext2_allocate_inode(ext2_fs *fs) {
    for (uint32_t group = 0; group < fs->bgd_count; group++) {
        uint8_t *bitmap = kmalloc(fs->block_size);
        acquire(&fs->bg_locks[group]);
        ext2_read_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);

        for (uint32_t i = 0; i < fs->sb->inodes_per_group; i++) {
            if (!bitmap_get(bitmap, i)) {
                bitmap_set(bitmap, i);
                ext2_write_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);
                kfree(bitmap);
                
                acquire(&fs->sb_lock);
                fs->sb->free_inodes_count--;
                ext2_write_sb(fs);
                release(&fs->sb_lock);

                fs->bgd_table[group].free_inodes--;
                ext2_write_bgd(fs, group, fs->bgd_table[group]);

                release(&fs->bg_locks[group]);
                return group * fs->sb->inodes_per_group + i + 1;
            }
        }
        release(&fs->bg_locks[group]);
        kfree(bitmap);
    }
    return 0;
}

void ext2_free_block(ext2_fs *fs, uint32_t block) {
    assert(block);
    block -= fs->sb->block_num;

    uint32_t group = block / fs->sb->blocks_per_group;
    uint32_t index = block % fs->sb->blocks_per_group;

    uint8_t *bitmap = kmalloc(fs->block_size);
    ext2_read_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);

    if (bitmap_get(bitmap, index)) {
        bitmap_clear(bitmap, index);
        ext2_write_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);

        acquire(&fs->sb_lock);
        fs->sb->free_blocks_count++;
        ext2_write_sb(fs);
        release(&fs->sb_lock);

        fs->bgd_table[group].free_blocks++;
        ext2_write_bgd(fs, group, fs->bgd_table[group]);
    }

    kfree(bitmap);
}

void ext2_free_inode(ext2_fs *fs, uint32_t ino) {
    assert(ino);
    
    uint32_t group = (ino - 1) / fs->sb->inodes_per_group;
    uint32_t index = (ino - 1) % fs->sb->inodes_per_group;

    uint8_t *bitmap = kmalloc(fs->block_size);
    acquire(&fs->bg_locks[group]);
    ext2_read_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);

    if (bitmap_get(bitmap, index)) {
        bitmap_clear(bitmap, index);
        ext2_write_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);

        acquire(&fs->sb_lock);
        fs->sb->free_inodes_count++;
        ext2_write_sb(fs);
        release(&fs->sb_lock);

        fs->bgd_table[group].free_inodes++;
        ext2_write_bgd(fs, group, fs->bgd_table[group]);
    }

    release(&fs->bg_locks[group]);
    kfree(bitmap);
}

int ext2_add_dirent(uint8_t *block_data, size_t block_size, const char *name, uint32_t in) {
    uint32_t offset = 0;
    uint32_t required = ALIGN_UP(sizeof(ext2_dirent) + strlen(name), 4);

    while (offset < block_size) {
        ext2_dirent *entry = (ext2_dirent *)(block_data + offset);

        if (!entry->total_size) {
            uint32_t available = block_size - offset;
            if (available < required)
                break;

            entry->inode = in;
            entry->name_len = strlen(name);
            entry->type = 1;
            entry->total_size = available;
            memcpy(entry->name, name, entry->name_len);
            return 0;
        }

        uint32_t used = ALIGN_UP(sizeof(ext2_dirent) + entry->name_len, 4);
        uint32_t remaining = entry->total_size - used;

        if (entry->inode != 0 && entry->total_size >= used + required) {
            entry->total_size = used;

            ext2_dirent *dirent = (ext2_dirent *)((uint8_t *)entry + used);
            dirent->inode = in;
            dirent->name_len = strlen(name);
            dirent->type = 1;
            dirent->total_size = remaining;
            memcpy(dirent->name, name, dirent->name_len);

            return 0;
        }

        offset += entry->total_size;
    }
    return -ENOSPC;
}

int ext2_remove_dirent(uint8_t *block_data, size_t block_size, uint32_t inode) {
    uint32_t offset = 0;
    ext2_dirent *prev = NULL;

    while (offset < block_size) {
        ext2_dirent *entry = (ext2_dirent *)(block_data + offset);

        if (!entry->total_size)
            break;

        if (entry->inode == inode) {
            if (prev) prev->total_size += entry->total_size;
            else entry->inode = 0;
            return 0;
        }

        prev = entry;
        offset += entry->total_size;
    }
    return -ENOENT;
}

int ext2_add_inode(ext2_fs *fs, uint32_t dir_inode, const char *name, uint32_t in) {
    ext2_inode inode;
    ext2_read_inode(fs, dir_inode, &inode);

    uint32_t total_blocks = inode.size ? (inode.size + fs->block_size - 1) / fs->block_size : 1;

    for (uint32_t i = 0; i < total_blocks && i < 12; i++) {
        if (inode.direct_block_ptr[i] == 0) {
            if (!(inode.direct_block_ptr[i] = ext2_allocate_block(fs)))
                return -ENOSPC;

            uint8_t *zero = kmalloc(fs->block_size);
            memset(zero, 0, fs->block_size);
            ext2_write_block(fs, inode.direct_block_ptr[i], zero, fs->block_size);
            kfree(zero);

            inode.size += fs->block_size;
            total_blocks = (inode.size + fs->block_size - 1) / fs->block_size;
            ext2_write_inode(fs, dir_inode, &inode);
        }

        uint8_t *block = kmalloc(fs->block_size);
        ext2_read_block(fs, inode.direct_block_ptr[i], block, fs->block_size);

        if (!ext2_add_dirent(block, fs->block_size, name, in)) {
            ext2_write_block(fs, inode.direct_block_ptr[i], block, fs->block_size);
            kfree(block);
            return 0;
        }
        kfree(block);
    }
    return -ENOSPC;
}

int ext2_remove_inode(ext2_fs *fs, uint32_t dir_inode, uint32_t in) {
    ext2_inode inode;
    ext2_read_inode(fs, dir_inode, &inode);

    uint32_t total_blocks = inode.size ? (inode.size + fs->block_size - 1) / fs->block_size : 1;
    
    for (uint32_t i = 0; i < total_blocks && i < 12; i++) {
        if (inode.direct_block_ptr[i] == 0)
            continue;

        uint8_t *block = kmalloc(fs->block_size);
        ext2_read_block(fs, inode.direct_block_ptr[i], block, fs->block_size);

        if (!ext2_remove_dirent(block, fs->block_size, in)) {
            ext2_write_block(fs, inode.direct_block_ptr[i], block, fs->block_size);
            kfree(block);
            return 0;
        }
        kfree(block);
    }
    return -ENOENT;
}

bool ext2_check_sequential(uint32_t blocks[], uint32_t count) {
    if (count == 0) return false;
    if (count <= 1) return blocks[0] != 0;
    if (!blocks[0]) return false;
    
    for (uint32_t i = 1; i < count; i++) {
        if (!blocks[i] || blocks[i] != (blocks[i - 1] + 1)) {
            return false;
        }
    }
    return true;
}

bool ext2_ensure_indirect_block(ext2_fs *fs, uint32_t *block) {
    if (*block)
        return true;
    if (!(*block = ext2_allocate_block(fs)))
        return false;
    
    uint32_t *zero = kmalloc(fs->block_size);
    memset(zero, 0, fs->block_size);
    ext2_write_block(fs, *block, zero, fs->block_size);
    kfree(zero);
    return true;
}

bool ext2_allocate_indirect_blocks(ext2_fs *fs, uint32_t *ptrs, uint32_t block, uint32_t offset, uint32_t count, uint32_t max_ptrs) {
    uint32_t need_alloc = 0;
    uint32_t indices[max_ptrs];
    
    uint32_t end = offset + count > max_ptrs ? max_ptrs : offset + count;
    for (uint32_t i = offset; i < end; i++) {
        if (!ptrs[i])
            indices[need_alloc++] = i;
    }
    
    if (need_alloc == 0)
        return true;
    
    uint32_t new_blocks[max_ptrs];
    if (ext2_allocate_blocks(fs, need_alloc, new_blocks) < need_alloc)
        return false;
    
    for (uint32_t i = 0; i < need_alloc; i++)
        ptrs[indices[i]] = new_blocks[i];
    
    ext2_write_block(fs, block, ptrs, fs->block_size);
    return true;
}

void ext2_transfer_blocks(ext2_fs *fs, uint32_t *block_ptrs, uint32_t offset, uint32_t count, uint8_t *buffer, bool write) {
    if (ext2_check_sequential(&block_ptrs[offset], count)) {
        if (write) ext2_write_block(fs, block_ptrs[offset], buffer, fs->block_size * count);
        else ext2_read_block(fs, block_ptrs[offset], buffer, fs->block_size * count);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            if (write) ext2_write_block(fs, block_ptrs[offset + i], buffer + i * fs->block_size, fs->block_size);
            else if (block_ptrs[offset + i]) ext2_read_block(fs, block_ptrs[offset + i], buffer + i * fs->block_size, fs->block_size);
        }
    }
}

void ext2_read_direct_blocks(ext2_fs *fs, uint32_t blocks[], void *buffer, uint32_t count) {
    if (ext2_check_sequential(blocks, count)) {
        ext2_read_block(fs, blocks[0], buffer, fs->block_size * count);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            if (blocks[i]) {
                ext2_read_block(fs, blocks[i], buffer + (i * fs->block_size), fs->block_size);
            } else {
                memset(buffer + (i * fs->block_size), 0, fs->block_size);
            }
        }
    }
}

void ext2_write_direct_blocks(ext2_fs *fs, uint32_t blocks[], void *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!blocks[i] && !(blocks[i] = ext2_allocate_block(fs)))
            return;
    }

    if (ext2_check_sequential(blocks, count)) {
        ext2_write_block(fs, blocks[0], buffer, fs->block_size * count);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            ext2_write_block(fs, blocks[i], buffer + (i * fs->block_size), fs->block_size);
        }
    }
}

uint32_t ext2_read_singly_blocks(ext2_fs *fs, uint32_t block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    if (!block)
        return 0;

    uint32_t singly_ptr = fs->block_size / sizeof(uint32_t);
    if (offset >= singly_ptr)
        return 0;
    
    count = offset + count > singly_ptr ? singly_ptr - offset : count;

    uint32_t *block_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, block, block_ptrs, fs->block_size);
    
    ext2_transfer_blocks(fs, block_ptrs, offset, count, buffer, false);
    
    kfree(block_ptrs);
    return count;
}

void ext2_write_singly_blocks(ext2_fs *fs, uint32_t *block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    uint32_t singly_ptr = fs->block_size / sizeof(uint32_t);
    
    if (!ext2_ensure_indirect_block(fs, block))
        return;

    uint32_t *block_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, *block, block_ptrs, fs->block_size);

    if (!ext2_allocate_indirect_blocks(fs, block_ptrs, *block, offset, count, singly_ptr)) {
        kfree(block_ptrs);
        return;
    }
    
    uint32_t len = offset + count > singly_ptr ? singly_ptr - offset : count;
    ext2_transfer_blocks(fs, block_ptrs, offset, len, buffer, true);

    kfree(block_ptrs);
}

uint32_t ext2_read_doubly_blocks(ext2_fs *fs, uint32_t block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    if (!block)
        return 0;

    uint32_t doubly_ptr = fs->block_size / sizeof(uint32_t);
    uint32_t *doubly_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, block, doubly_ptrs, fs->block_size);

    uint32_t read = 0;
    for (uint32_t i = offset / doubly_ptr; i < doubly_ptr && read < count; i++) {
        uint32_t singly_offset = (i == offset / doubly_ptr) ? offset % doubly_ptr : 0;
        uint32_t singly_count = count - read > doubly_ptr - singly_offset ? doubly_ptr - singly_offset : count - read;
        
        if (doubly_ptrs[i]) read += ext2_read_singly_blocks(fs, doubly_ptrs[i], buffer + read * fs->block_size, singly_offset, singly_count);
        else read += singly_count;
    }

    kfree(doubly_ptrs);
    return read;
}

void ext2_write_doubly_blocks(ext2_fs *fs, uint32_t *block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    uint32_t doubly_ptr = fs->block_size / sizeof(uint32_t);
    
    if (!ext2_ensure_indirect_block(fs, block))
        return;

    uint32_t *doubly_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, *block, doubly_ptrs, fs->block_size);

    uint32_t first = offset / doubly_ptr;
    uint32_t last = (offset + count - 1) / doubly_ptr;
    
    uint32_t need_singly = 0;
    uint32_t singly_indices[doubly_ptr];
    for (uint32_t i = first; i <= last && i < doubly_ptr; i++) {
        if (!doubly_ptrs[i])
            singly_indices[need_singly++] = i;
    }
    
    if (need_singly > 0) {
        uint32_t new_blocks[doubly_ptr];
        if (ext2_allocate_blocks(fs, need_singly, new_blocks) < need_singly) {
            kfree(doubly_ptrs);
            return;
        }
        for (uint32_t i = 0; i < need_singly; i++) {
            doubly_ptrs[singly_indices[i]] = new_blocks[i];
            
            uint32_t *zero = kmalloc(fs->block_size);
            memset(zero, 0, fs->block_size);
            ext2_write_block(fs, new_blocks[i], zero, fs->block_size);
            kfree(zero);
        }
        ext2_write_block(fs, *block, doubly_ptrs, fs->block_size);
    }
    
    uint32_t written = 0;
    for (uint32_t i = first; i < doubly_ptr && written < count; i++) {
        uint32_t singly_offset = (i == first) ? offset % doubly_ptr : 0;
        uint32_t singly_count = count - written > doubly_ptr - singly_offset ? doubly_ptr - singly_offset : count - written;

        ext2_write_singly_blocks(fs, &doubly_ptrs[i], buffer + written * fs->block_size, singly_offset, singly_count);
        written += singly_count;
    }

    kfree(doubly_ptrs);
}

void ext2_read_inode_blocks(ext2_fs *fs, ext2_inode *in, uint8_t *buffer, uint32_t block, uint32_t block_count) {
    uint32_t blocks_per_singly = fs->block_size / 4;
    uint32_t blocks_per_doubly = blocks_per_singly * blocks_per_singly;
    uint32_t current = block;
    uint32_t remaining = block_count;
    uint32_t offset = 0;
    
    if (current < 12 && remaining > 0) {
        uint32_t count = remaining < 12 - current ? remaining : 12 - current;
        ext2_read_direct_blocks(fs, &in->direct_block_ptr[current], buffer + offset, count);

        offset += count * fs->block_size;
        current += count;
        remaining -= count;
    }

    if (current < 12 + blocks_per_singly && remaining > 0 && in->singly_block_ptr != 0) {
        uint32_t count = current + remaining > 12 + blocks_per_singly ? 12 + blocks_per_singly - current : remaining;
        uint32_t read = ext2_read_singly_blocks(fs, in->singly_block_ptr, buffer + offset, current >= 12 ? current - 12 : 0, count);

        offset += read * fs->block_size;
        current += read;
        remaining -= read;
    }

    if (current < 12 + blocks_per_singly + blocks_per_doubly && remaining > 0 && in->doubly_block_ptr != 0) {
        ext2_read_doubly_blocks(fs, in->doubly_block_ptr, buffer + offset,
            current >= 12 + blocks_per_singly ? current - (12 + blocks_per_singly) : 0, remaining);
    }
}

void ext2_write_inode_blocks(ext2_fs *fs, ext2_inode *in, uint8_t *buffer, uint32_t block, uint32_t block_count) {
    uint32_t blocks_per_singly = fs->block_size / sizeof(uint32_t);
    uint32_t blocks_per_doubly = blocks_per_singly * blocks_per_singly;
    uint32_t current = block;
    uint32_t remaining = block_count;
    uint32_t offset = 0;

    if (current < 12 && remaining > 0) {
        uint32_t count = (remaining < 12 - current) ? remaining : 12 - current;
        ext2_write_direct_blocks(fs, &in->direct_block_ptr[current], buffer + offset, count);

        offset += count * fs->block_size;
        current += count;
        remaining -= count;
    }

    if (current < 12 + blocks_per_singly && remaining > 0) {
        uint32_t indirect_offset = current > 12 ? current - 12 : 0;
        uint32_t count = (remaining < blocks_per_singly - indirect_offset) ? remaining : blocks_per_singly - indirect_offset;
        ext2_write_singly_blocks(fs, &in->singly_block_ptr, buffer + offset, indirect_offset, count);

        offset += count * fs->block_size;
        current += count;
        remaining -= count;
    }

    if (current < 12 + blocks_per_singly + blocks_per_doubly && remaining > 0) {
        uint32_t doubly_offset = current > (12 + blocks_per_singly) ? current - (12 + blocks_per_singly) : 0;
        uint32_t count = remaining;
        ext2_write_doubly_blocks(fs, &in->doubly_block_ptr, buffer + offset, doubly_offset, count);
    }
}

void ext2_free_inode_blocks(ext2_fs *fs, ext2_inode *in) {
    uint32_t blocks_per_singly = fs->block_size / 4;

    for (uint32_t i = 0; i < 12; i++) {
        if (in->direct_block_ptr[i]) {
            ext2_free_block(fs, in->direct_block_ptr[i]);
            in->direct_block_ptr[i] = 0;
        }
    }

    if (in->singly_block_ptr) {
        uint32_t *entries = kmalloc(fs->block_size);
        ext2_read_block(fs, in->singly_block_ptr, entries, fs->block_size);

        for (uint32_t i = 0; i < blocks_per_singly; i++) {
            if (entries[i])
                ext2_free_block(fs, entries[i]);
        }

        kfree(entries);
        ext2_free_block(fs, in->singly_block_ptr);
        in->singly_block_ptr = 0;
    }

    if (in->doubly_block_ptr) {
        uint32_t *doubly_ptrs = kmalloc(fs->block_size);
        ext2_read_block(fs, in->doubly_block_ptr, doubly_ptrs, fs->block_size);

        for (uint32_t i = 0; i < blocks_per_singly; i++) {
            if (doubly_ptrs[i]) {
                uint32_t *singly_ptrs = kmalloc(fs->block_size);
                ext2_read_block(fs, doubly_ptrs[i], singly_ptrs, fs->block_size);

                for (uint32_t j = 0; j < blocks_per_singly; j++) {
                    if (singly_ptrs[j])
                        ext2_free_block(fs, singly_ptrs[j]);
                }

                kfree(singly_ptrs);
                ext2_free_block(fs, doubly_ptrs[i]);
            }
        }

        kfree(doubly_ptrs);
        ext2_free_block(fs, in->doubly_block_ptr);
        in->doubly_block_ptr = 0;
    }
}

long ext2_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -ENOENT;
    assert(fs->sb->signature == 0xef53);
    
    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);
    inode.last_access_time = now();

    if (offset >= inode.size)
        return 0;
    if (offset + len > inode.size)
        len = inode.size - offset;

    uint32_t block = offset / fs->block_size;
    uint32_t count = ((offset + len - 1) / fs->block_size) - block + 1;

    uint8_t *buf = kmalloc(count * fs->block_size);
    ext2_read_inode_blocks(fs, &inode, buf, block, count);
    memcpy(buffer, buf + (offset % fs->block_size), len);
    ext2_write_inode(fs, node->inode, &inode);

    kfree(buf);
    return len;
}

long ext2_write(vfs_node_t *node, const void *buffer, long offset, size_t len) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -ENOENT;
    assert(fs->sb->signature == 0xef53);
    
    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);
    inode.last_access_time = inode.mod_time = now();

    if (node->type == VFS_SYMLINK) {
        if (len > 60) {
            dprintf(LOG_DEBUG, "\033[93mext2:\033[0m slow symlinks not supported for '%s'\n", node->name);
            return -ENOSYS;
        }
        inode.size = len;
        memcpy((char *)inode.direct_block_ptr, buffer, len);
        ext2_write_inode(fs, node->inode, &inode);
        return len;
    }

    if (offset == -1)
        offset = inode.size;
    if (offset + len > inode.size)
        inode.size = offset + len;

    uint32_t block = offset / fs->block_size;
    uint32_t count = DIV_CEILING(offset + len, fs->block_size) - block;

    uint8_t *buf = kmalloc(count * fs->block_size);
    
    if (((offset % fs->block_size) != 0 || ((offset + len) % fs->block_size) != 0) &&
        block * fs->block_size < inode.size && count > 0) {
        ext2_read_inode_blocks(fs, &inode, buf, block, count);
    } else {
        memset(buf, 0, count * fs->block_size);
    }

    memcpy(buf + (offset % fs->block_size), buffer, len);
    ext2_write_inode_blocks(fs, &inode, buf, block, count);
    ext2_write_inode(fs, node->inode, &inode);

    kfree(buf);
    node->size = inode.size;
    return len;
}

vfs_result_t ext2_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    ext2_fs *fs = parent->device;
    if (!fs)
        return (vfs_result_t){ NULL, -ENODEV };
    assert(fs->sb->signature == 0xef53);

    ext2_inode inode;
    memset(&inode, 0, sizeof inode);
    inode.type_perms = type == VFS_SYMLINK ? EXT_SYM_LINK | 0777 : type == VFS_DIRECTORY ? EXT_DIRECTORY | 0755 : EXT_FILE | 0644;
    inode.size = 0;
    inode.last_access_time = inode.creation_time = inode.mod_time = now();
    inode.hard_link_count = type == VFS_DIRECTORY ? 2 : 1;
    if (type == VFS_DIRECTORY) {
        inode.direct_block_ptr[0] = ext2_allocate_block(fs);
        inode.size = fs->block_size;

        uint8_t *zero = kmalloc(fs->block_size);
        memset(zero, 0, fs->block_size);
        ext2_write_block(fs, inode.direct_block_ptr[0], zero, fs->block_size);
        kfree(zero);
    }

    vfs_node_t *node = vfs_create_node(name, type);
    node->size = 0;
    node->inode = ext2_allocate_inode(fs);
    assert(node->inode);
    ext2_write_inode(fs, node->inode, &inode);
    node->ops = &ext2_ops;
    node->device = fs;
    ext2_add_inode(fs, parent->inode, name, node->inode);

    if (type == VFS_DIRECTORY) {
        ext2_add_inode(fs, node->inode, ".", node->inode);
        ext2_add_inode(fs, node->inode, "..", parent->inode);

        ext2_inode parent_inode;
        ext2_read_inode(fs, parent->inode, &parent_inode);
        parent_inode.hard_link_count++;
        ext2_write_inode(fs, parent->inode, &parent_inode);
    }

    vfs_add_node(parent, node);
    return (vfs_result_t){ node, 0 };
}

long ext2_remove(vfs_node_t *node) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -EIO;
    assert(fs->sb->signature == 0xef53);
    if (node->children->length > 2)
        return -ENOTEMPTY;

    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);

    if (ext2_get_type(inode.type_perms) == VFS_DIRECTORY) {
        ext2_remove_inode(fs, node->inode, node->inode);
        ext2_remove_inode(fs, node->inode, node->parent->inode);
        
        ext2_inode parent_inode;
        ext2_read_inode(fs, node->parent->inode, &parent_inode);
        parent_inode.hard_link_count--;
        ext2_write_inode(fs, node->parent->inode, &parent_inode);
    }

    if (ext2_get_type(inode.type_perms) != VFS_SYMLINK || inode.size > 60)
        ext2_free_inode_blocks(fs, &inode);

    int ret = ext2_remove_inode(fs, node->parent->inode, node->inode);
    if (ret < 0)
        return ret;
    ext2_free_inode(fs, node->inode);
    return 0;
}

long ext2_rename(vfs_node_t *node, vfs_node_t *parent, const char *name) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -EIO;
    assert(fs->sb->signature == 0xef53);
    assert(node->device == parent->device);

    ext2_inode inode;

    if (node->type == VFS_DIRECTORY && node->parent != parent) {
        ext2_read_inode(fs, node->parent->inode, &inode);
        inode.hard_link_count--;
        ext2_write_inode(fs, node->parent->inode, &inode);
        
        ext2_read_inode(fs, parent->inode, &inode);
        inode.hard_link_count++;
        ext2_write_inode(fs, parent->inode, &inode);
        
        ext2_remove_inode(fs, node->inode, node->parent->inode);
        ext2_add_inode(fs, node->inode, "..", parent->inode);
    }
    
    int ret = ext2_remove_inode(fs, node->parent->inode, node->inode);
    if (ret < 0)
        return ret;
    
    ret = ext2_add_inode(fs, parent->inode, name, node->inode);
    if (ret < 0) {
        ext2_add_inode(fs, node->parent->inode, node->name, node->inode);
        return ret;
    }
    
    ext2_read_inode(fs, node->parent->inode, &inode);
    inode.mod_time = now();
    ext2_write_inode(fs, node->parent->inode, &inode);
    
    if (node->parent != parent) {
        ext2_read_inode(fs, parent->inode, &inode);
        inode.mod_time = now();
        ext2_write_inode(fs, parent->inode, &inode);
    }
    
    return 0;
}

long ext2_chmod(vfs_node_t *node, unsigned int mode) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -EIO;
    assert(fs->sb->signature == 0xef53);

    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);
    inode.type_perms = (inode.type_perms & 0xF000) | (mode & 0x0FFF);
    ext2_write_inode(fs, node->inode, &inode);
    
    return 0;
}

enum vfs_node_type ext2_get_type(uint16_t type_perms) {
    switch (type_perms & 0xF000) {
        case EXT_FILE:
            return VFS_FILE;
        case EXT_DIRECTORY:
            return VFS_DIRECTORY;
        case EXT_CHAR_DEV:
            return VFS_CHARDEVICE;
        case EXT_BLOCK_DEV:
            return VFS_BLOCKDEVICE;
        case EXT_SYM_LINK:
            return VFS_SYMLINK;
        default:
            return VFS_NONE;
    }
}

uint16_t ext2_get_perms(uint16_t type_perms) {
    return type_perms & 0x0FFF;
}

vfs_node_t *ext2_create_symlink_node(ext2_fs *fs, const char *name, ext2_inode *inode) {
    assert(inode->size <= MAX_PATH);

    char target[inode->size + 1];
    if (inode->size <= 60) {
        memcpy(target, (char *)inode->direct_block_ptr, inode->size);
    } else {
        uint8_t *buf = kmalloc(fs->block_size);
        ext2_read_inode_blocks(fs, inode, buf, 0, 1);
        memcpy(target, buf, inode->size);
        kfree(buf);
    }

    target[inode->size] = '\0';
    return vfs_create_symlink(name, target);
}

void ext2_mount_directory(ext2_fs *fs, uint8_t *block_data, size_t block_size, vfs_node_t *parent) {
    uint32_t offset = 0;

    while (offset < block_size) {
        ext2_dirent *entry = (ext2_dirent *)(block_data + offset);
        if (entry->inode == 0 || entry->total_size == 0)
            break;
        
        char name[entry->name_len + 1];
        memcpy(name, entry->name, entry->name_len);
        name[entry->name_len] = '\0';

        ext2_inode child;
        ext2_read_inode(fs, entry->inode, &child);

        enum vfs_node_type type = ext2_get_type(child.type_perms);
        vfs_node_t *node = type == VFS_SYMLINK ? ext2_create_symlink_node(fs, name, &child) : vfs_create_node(name, type);
        if (node) {
            node->size = child.size;
            node->perms = ext2_get_perms(child.type_perms);
            node->inode = entry->inode;
            node->ops = &ext2_ops;
            node->atime = child.last_access_time;
            node->ctime = child.creation_time;
            node->mtime = child.mod_time;
            node->blocks = child.sector_count;
            node->perms = ext2_get_perms(child.type_perms);

            if (type != VFS_SYMLINK)
                node->device = fs;

            vfs_add_node(parent, node);
        }

        offset += entry->total_size;
    }
    parent->flags |= EXT2_FS_FLAGS_MOUNTED;
}

void ext2_mount_node(ext2_fs *fs, vfs_node_t *parent, uint32_t in) {
    ext2_inode inode;
    ext2_read_inode(fs, in, &inode);

    if (inode.size == 0) return;

    uint8_t *blocks = kmalloc(inode.size);
    ext2_read_inode_blocks(fs, &inode, blocks, 0, (inode.size + fs->block_size - 1) / fs->block_size);
    ext2_mount_directory(fs, blocks, inode.size, parent);
    kfree(blocks);
}

vfs_result_t ext2_open(vfs_node_t *node, int flags) {
    (void)flags;
    ext2_fs *fs = node->device;
    if (!fs)
        return (vfs_result_t){ NULL, -ENODEV };
    assert(fs->sb->signature == 0xef53);

    if (node->type != VFS_DIRECTORY)
        return (vfs_result_t){ node, 0 };
    if (!(node->flags & EXT2_FS_FLAGS_MOUNTED))
        ext2_mount_node(fs, node, node->inode);
    return (vfs_result_t){ node, 0 };
}

long ext2_mount(vfs_node_t *node, vfs_node_t *device, long flags) {
    (void)flags;
    if (!device)
        return -EINVAL;

    ext2_fs *fs = kmalloc(sizeof(ext2_fs));
    fs->device = device;
    fs->sb = (ext2_sb *)kmalloc(ALIGN_UP(sizeof(ext2_sb), 512));
    vfs_read(device, fs->sb, 1024, ALIGN_UP(sizeof(ext2_sb), 512));

    char *dev_path = vfs_resolve_path(device), *node_path = vfs_resolve_path(node);
    if (fs->sb->signature != 0xef53) {
        dprintf(LOG_ERR, "\033[93mext2:\033[0m %s: not an ext2 partition\n", dev_path);
        kfree(dev_path);
        kfree(node_path);
        kfree(fs->sb);
        kfree(fs);
        return -EINVAL;
    }
    dprintf(LOG_DEBUG, "\033[93mext2:\033[0m mounting %s to %s\n", dev_path, node_path);
    if (fs->sb->req_features) {
        dprintf(LOG_ERR, "\033[93mext2:\033[0m %s: unsupported features 0x%x\n", dev_path, fs->sb->req_features);
        kfree(dev_path);
        kfree(node_path);
        kfree(fs->sb);
        kfree(fs);
        return -EOPNOTSUPP;
    }
    fs->block_size = 1024 << fs->sb->log2_block;
    fs->bgd_count = (fs->sb->blocks_count / fs->sb->blocks_per_group) ?: 1;
    fs->bgd_block = fs->sb->block_num + 1;
    fs->bgd_table = (ext2_bgd *)kmalloc(ALIGN_UP(fs->bgd_count * sizeof(ext2_bgd), 512));
    fs->inode_size = fs->sb->inode_size;
    ext2_read_block(fs, fs->bgd_block, fs->bgd_table, ALIGN_UP(fs->bgd_count * sizeof(ext2_bgd), 512));
    fs->sb_lock = 0;
    fs->bg_locks = kmalloc(fs->bgd_count * sizeof(spinlock_t));
    memset((void *)fs->bg_locks, 0, fs->bgd_count * sizeof(spinlock_t));
    
    node->inode = 2;
    node->device = fs;
    node->ops = &ext2_ops;
    
    kfree(node_path);
    kfree(dev_path);
    return 0;
}

void ext2_unmount_recursive(vfs_node_t *node) {
    foreach_safe(i, node->children) {
        vfs_node_t *child = i->value;
        if (child->type == VFS_DIRECTORY && strcmp(child->name, ".") && strcmp(child->name, ".."))
            ext2_unmount_recursive(child);
        if (child->device == node->device)
            vfs_remove_node(child);
    }
}

long ext2_unmount(vfs_node_t *node, long flags) {
    (void)flags;
    
    ext2_fs *fs = node->device;
    if (!fs)
        return -EINVAL;
    
    char *node_path = vfs_resolve_path(node);
    dprintf(LOG_DEBUG, "\033[93mext2:\033[0m unmounting %s\n", node_path);
    kfree(node_path);
    
    ext2_unmount_recursive(node);

    node->device = NULL;
    node->ops = NULL;
    node->flags &= ~EXT2_FS_FLAGS_MOUNTED;
    
    kfree(fs->bgd_table);
    kfree((void *)fs->bg_locks);
    kfree(fs->sb);
    kfree(fs);
    
    return 0;
}

vfs_mount_ops_t ext2_mount_ops = {
    .type    = "ext2",
    .nodev   = false,
    .mount   = ext2_mount,
    .unmount = ext2_unmount
};

int init() {
    vfs_register(&ext2_mount_ops);

    if (!args_contains("root"))
        return 0;
    return vfs_mount(vfs_get_root(), "ext2", vfs_lookup(NULL, args_value("root"), false, VFS_NONE).node, 0);
}

int fini() {
    dprintf(LOG_INFO, "\033[93mext2:\033[0m Goodbye!\n");
    return 0;
}

struct Module metadata = {
    .name = "ext2",
    .init = init,
    .fini = fini
};