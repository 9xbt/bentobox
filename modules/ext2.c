#include <stdint.h>
#include <errno.h>
#include <kernel/assert.h>
#include <kernel/bitmap.h>
#include <kernel/malloc.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
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
    vfs_node_t *sda;
    ext2_sb    *sb;
    ext2_bgd   *bgd_table;
    uint32_t block_size;
    uint32_t bgd_count;
    uint32_t bgd_block;
    uint32_t inode_size;
} ext2_fs;

#define EXT2_FS_FLAGS_MOUNTED 0x1

struct vfs_node *ext2_create(struct vfs_node *parent, const char *name);
struct vfs_node *ext2_mkdir(struct vfs_node *parent, const char *name);
struct vfs_node *ext2_open(vfs_node_t *node, int flags);
long ext2_remove(struct vfs_node *node);

void ext2_mount(ext2_fs *fs, struct vfs_node *parent, uint32_t in);

void ext2_read_block(ext2_fs *fs, uint32_t block, void *buffer, uint32_t count) {
    assert(block);
    vfs_read(fs->sda, buffer, block * fs->block_size, count);
}

void ext2_write_block(ext2_fs *fs, uint32_t block, void *buffer, uint32_t count) {
    assert(block);
    vfs_write(fs->sda, buffer, block * fs->block_size, count);
}

void ext2_write_bgd(ext2_fs *fs, uint32_t group, ext2_bgd bgd) {
    fs->bgd_table[group] = bgd;
    ext2_write_block(fs, fs->bgd_block, fs->bgd_table, fs->block_size);
}

void ext2_write_sb(ext2_fs *fs) {
    vfs_write(fs->sda, fs->sb, 1024, sizeof(ext2_sb));
}

void ext2_read_inode(ext2_fs *fs, uint32_t inode, ext2_inode *in) {
    assert(inode);

    inode--;
    uint32_t block_group = inode / fs->sb->inodes_per_group;
    uint32_t inode_index = inode % fs->sb->inodes_per_group;
    uint32_t inode_block = (inode_index * fs->inode_size) / fs->block_size;

    assert(block_group < fs->bgd_count);

    uint8_t buffer[fs->block_size];
    ext2_read_block(fs, fs->bgd_table[block_group].inode_table + inode_block, buffer, fs->block_size);
    memcpy(in, buffer + (inode_index % (fs->block_size / fs->inode_size)) * fs->inode_size, fs->inode_size);
}

void ext2_write_inode(ext2_fs *fs, uint32_t inode, ext2_inode *in) {
    assert(inode);

    inode--;
    uint32_t block_group = inode / fs->sb->inodes_per_group;
    uint32_t inode_index = inode % fs->sb->inodes_per_group;
    uint32_t inode_block = (inode_index * fs->inode_size) / fs->block_size;
    uint32_t inode_offset = (inode_index % (fs->block_size / fs->inode_size)) * fs->inode_size;

    assert(block_group < fs->bgd_count);

    uint8_t buffer[fs->block_size];
    ext2_read_block(fs, fs->bgd_table[block_group].inode_table + inode_block, buffer, fs->block_size);
    memcpy(buffer + inode_offset, in, fs->inode_size);
    ext2_write_block(fs, fs->bgd_table[block_group].inode_table + inode_block, buffer, fs->block_size);
}

uint32_t ext2_allocate_block(ext2_fs *fs) {
    for (uint32_t group = 0; group < fs->bgd_count; group++) {
        uint8_t *bitmap = kmalloc(fs->block_size);
        ext2_read_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);

        for (uint32_t i = 0; i < fs->sb->blocks_per_group; i++) {
            if (!bitmap_get(bitmap, i)) {
                bitmap_set(bitmap, i);
                ext2_write_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);
                kfree(bitmap);

                fs->sb->free_blocks_count--;
                fs->bgd_table[group].free_blocks--;
                ext2_write_sb(fs);
                ext2_write_bgd(fs, group, fs->bgd_table[group]);
                return group * fs->sb->blocks_per_group + i;
            }
        }
        kfree(bitmap);
    }
    return 0;
}

uint32_t ext2_allocate_inode(ext2_fs *fs) {
    for (uint32_t group = 0; group < fs->bgd_count; group++) {
        uint8_t *bitmap = kmalloc(fs->block_size);
        ext2_read_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);

        for (uint32_t i = 0; i < fs->sb->inodes_per_group; i++) {
            if (!bitmap_get(bitmap, i)) {
                bitmap_set(bitmap, i);
                ext2_write_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);
                kfree(bitmap);
                
                fs->sb->free_inodes_count--;
                fs->bgd_table[group].free_inodes--;
                ext2_write_sb(fs);
                ext2_write_bgd(fs, group, fs->bgd_table[group]);
                return group * fs->sb->inodes_per_group + i + 1;
            }
        }
        kfree(bitmap);
    }
    return 0;
}

void ext2_free_block(ext2_fs *fs, uint32_t block) {
    uint32_t group = block / fs->sb->blocks_per_group;
    uint32_t index = block % fs->sb->blocks_per_group;

    uint8_t *bitmap = kmalloc(fs->block_size);
    ext2_read_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);

    if (bitmap_get(bitmap, index)) {
        bitmap_clear(bitmap, index);
        ext2_write_block(fs, fs->bgd_table[group].block_bitmap, bitmap, fs->block_size);

        fs->sb->free_blocks_count++;
        fs->bgd_table[group].free_blocks++;
        ext2_write_sb(fs);
        ext2_write_bgd(fs, group, fs->bgd_table[group]);
    }

    kfree(bitmap);
}

void ext2_free_inode(ext2_fs *fs, uint32_t ino) {
    uint32_t group = (ino - 1) / fs->sb->inodes_per_group;
    uint32_t index = (ino - 1) % fs->sb->inodes_per_group;

    uint8_t *bitmap = kmalloc(fs->block_size);
    ext2_read_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);

    if (bitmap_get(bitmap, index)) {
        bitmap_clear(bitmap, index);
        ext2_write_block(fs, fs->bgd_table[group].inode_bitmap, bitmap, fs->block_size);

        fs->sb->free_inodes_count++;
        fs->bgd_table[group].free_inodes++;
        ext2_write_sb(fs);
        ext2_write_bgd(fs, group, fs->bgd_table[group]);
    }

    kfree(bitmap);
}

int ext2_add_dirent(ext2_fs *fs, uint8_t *block_data, size_t block_size, const char *name, uint32_t in) {
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

int ext2_remove_dirent(ext2_fs *fs, uint8_t *block_data, size_t block_size, uint32_t inode) {
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

        if (!ext2_add_dirent(fs, block, fs->block_size, name, in)) {
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

        if (!ext2_remove_dirent(fs, block, fs->block_size, in)) {
            ext2_write_block(fs, inode.direct_block_ptr[i], block, fs->block_size);
            kfree(block);
            return 0;
        }
        kfree(block);
    }
    return -ENOENT;
}

bool ext2_check_sequential(uint32_t blocks[], uint32_t count) {
    if (count <= 1) return true;
    if (!blocks[0]) return false;
    
    for (uint32_t i = 1; i < count; i++) {
        if (!blocks[i] || blocks[i] != (blocks[i - 1] + 1)) {
            return false;
        }
    }
    return true;
}

void ext2_read_direct_blocks(ext2_fs *fs, uint32_t blocks[], void *buffer, uint32_t count) {
    if (ext2_check_sequential(blocks, count)) {
        ext2_read_block(fs, blocks[0], buffer, fs->block_size * count);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            if (blocks[i]) {
                ext2_read_block(fs, blocks[i], buffer + (i * fs->block_size), fs->block_size);
            }
        }
    }
}

void ext2_write_direct_blocks(ext2_fs *fs, uint32_t blocks[], void *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!blocks[i] && !(blocks[i] = ext2_allocate_block(fs)))
            return;
        ext2_write_block(fs, blocks[i], buffer + (i * fs->block_size), fs->block_size);
    }
}

uint32_t ext2_read_singly_blocks(ext2_fs *fs, uint32_t block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    if (!block)
        return 0;

    uint32_t singly_ptr = fs->block_size / sizeof(uint32_t);
    if (offset >= singly_ptr)
        return 0;
    if (offset + count > singly_ptr)
        count = singly_ptr - offset;

    uint32_t *block_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, block, block_ptrs, fs->block_size);

    if (ext2_check_sequential(&block_ptrs[offset], count)) {
        ext2_read_block(fs, block_ptrs[offset], buffer, fs->block_size * count);
        kfree(block_ptrs);
        return count;
    }

    uint32_t i;
    for (i = 0; i < count; i++) {
        if (block_ptrs[offset + i]) {
            ext2_read_block(fs, block_ptrs[offset + i], buffer + (i * fs->block_size), fs->block_size);
        }
    }

    kfree(block_ptrs);
    return i;
}

void ext2_write_singly_blocks(ext2_fs *fs, uint32_t *block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    uint32_t singly_ptr = fs->block_size / sizeof(uint32_t);
    if (!block[0] && !(block[0] = ext2_allocate_block(fs)))
        return;

    uint32_t *block_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, *block, block_ptrs, fs->block_size);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = offset + i;
        if (index >= singly_ptr)
            break;
        if (!block_ptrs[index] && !(block_ptrs[index] = ext2_allocate_block(fs)))
            return;
        ext2_write_block(fs, block_ptrs[index], buffer + (i * fs->block_size), fs->block_size);
    }

    ext2_write_block(fs, *block, block_ptrs, fs->block_size);
    kfree(block_ptrs);
}

uint32_t ext2_read_doubly_blocks(ext2_fs *fs, uint32_t block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    if (!block)
        return 0;

    uint32_t doubly_ptr = fs->block_size / sizeof(uint32_t);
    uint32_t *doubly_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, block, doubly_ptrs, fs->block_size);

    uint32_t read = 0;
    uint32_t first_singly_table = offset / doubly_ptr;
    uint32_t last_singly_table = (offset + count - 1) / doubly_ptr;
    
    if (first_singly_table == last_singly_table || 
        ext2_check_sequential(&doubly_ptrs[first_singly_table], last_singly_table - first_singly_table + 1))
    {
        for (uint32_t i = first_singly_table; i <= last_singly_table && read < count; i++) {
            uint32_t singly_offset = (i == first_singly_table) ? offset % doubly_ptr : 0;
            uint32_t singly_count = (count - read > doubly_ptr - singly_offset) ? doubly_ptr - singly_offset : count - read;
            
            read += doubly_ptrs[i] ? ext2_read_singly_blocks(fs, doubly_ptrs[i], buffer + read * fs->block_size, singly_offset, singly_count) : singly_count;
        }
    } else {
        for (uint32_t i = offset / doubly_ptr; i < doubly_ptr && read < count; i++) {
            uint32_t singly_offset = (i == offset / doubly_ptr) ? offset % doubly_ptr : 0;
            uint32_t singly_count = (count - read > doubly_ptr - singly_offset) ? doubly_ptr - singly_offset : count - read;
            
            read += doubly_ptrs[i] ? ext2_read_singly_blocks(fs, doubly_ptrs[i], buffer + read * fs->block_size, singly_offset, singly_count) : singly_count;
        }
    }

    kfree(doubly_ptrs);
    return read;
}

void ext2_write_doubly_blocks(ext2_fs *fs, uint32_t *block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    uint32_t doubly_ptr = fs->block_size / sizeof(uint32_t);
    uint32_t *doubly_ptrs;

    if (!block[0] && !(block[0] = ext2_allocate_block(fs)))
        return;

    doubly_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, *block, doubly_ptrs, fs->block_size);

    uint32_t written = 0;
    for (uint32_t i = offset / doubly_ptr; i < doubly_ptr && written < count; i++) {
        uint32_t singly_offset = (i == offset / doubly_ptr) ? offset % doubly_ptr : 0;
        uint32_t singly_count = (count - written > doubly_ptr - singly_offset) ? doubly_ptr - singly_offset : count - written;

        ext2_write_singly_blocks(fs, &doubly_ptrs[i], buffer + (written * fs->block_size), singly_offset, singly_count);
        written += singly_count;
    }

    ext2_write_block(fs, *block, doubly_ptrs, fs->block_size);
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

long ext2_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
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

long ext2_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -ENOENT;
    assert(fs->sb->signature == 0xef53);
    
    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);
    inode.last_access_time = now();
    inode.mod_time = now();

    if (offset == -1)
        offset = inode.size;
    if (offset + len < inode.size || offset + len > inode.size)
        inode.size = offset + len;

    uint32_t block = offset / fs->block_size;
    uint32_t count = inode.size ? DIV_CEILING(offset + len, fs->block_size) - (offset / fs->block_size) : 0;

    uint8_t *buf = kmalloc(count * fs->block_size);
    if (count > 0 && (size_t)offset < inode.size - len) {
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

struct vfs_node *ext2_create(struct vfs_node *parent, const char *name) {
    ext2_fs *fs = parent->device;
    if (!fs)
        return NULL;
    assert(fs->sb->signature == 0xef53);

    ext2_inode inode;
    memset(&inode, 0, sizeof inode);
    inode.type_perms = EXT_FILE | 0644;
    inode.size = 0;
    inode.last_access_time = inode.creation_time = inode.mod_time = now();
    inode.hard_link_count = 1;

    struct vfs_node *node = vfs_create_node(name, VFS_FILE);
    node->size = 0;
    node->inode = ext2_allocate_inode(fs);
    assert(node->inode);
    ext2_write_inode(fs, node->inode, &inode);
    node->create = ext2_create;
    node->remove = ext2_remove;
    node->mkdir = ext2_mkdir;
    node->device = fs;
    node->read = ext2_read;
    node->write = ext2_write;
    vfs_add_node(parent, node);

    ext2_add_inode(fs, parent->inode, name, node->inode);
    return node;
}

long ext2_remove(struct vfs_node *node) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -EIO;
    assert(fs->sb->signature == 0xef53);

    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);
    ext2_free_inode_blocks(fs, &inode);

    return ext2_remove_inode(fs, node->parent->inode, node->inode);
}

struct vfs_node *ext2_mkdir(struct vfs_node *parent, const char *name) {
    ext2_fs *fs = parent->device;
    if (!fs)
        return NULL;
    assert(fs->sb->signature == 0xef53);

    ext2_inode inode;
    memset(&inode, 0, sizeof inode);
    inode.type_perms = EXT_DIRECTORY | 0755;
    inode.size = 0;
    inode.last_access_time = inode.creation_time = inode.mod_time = now();
    inode.hard_link_count = 2;
    inode.direct_block_ptr[0] = ext2_allocate_block(fs);

    struct vfs_node *node = vfs_create_node(name, VFS_DIRECTORY);
    node->size = 0;
    node->inode = ext2_allocate_inode(fs);
    ext2_write_inode(fs, node->inode, &inode);
    node->create = ext2_create;
    node->remove = ext2_remove;
    node->mkdir = ext2_mkdir;
    node->device = fs;
    node->read = ext2_read;
    node->write = ext2_write;
    vfs_add_node(parent, node);

    ext2_add_inode(fs, parent->inode, name, node->inode);
    ext2_add_inode(fs, node->inode, ".", node->inode);
    ext2_add_inode(fs, node->inode, "..", parent->inode);

    ext2_inode parent_inode;
    ext2_read_inode(fs, parent->inode, &parent_inode);
    parent_inode.hard_link_count++;
    ext2_write_inode(fs, parent->inode, &parent_inode);
    return node;
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

struct vfs_node *ext2_create_symlink_node(ext2_fs *fs, const char *name, ext2_inode *inode) {
    char *target = NULL;
    if (inode->size <= 60) {
        target = kmalloc(inode->size + 1);
        memcpy(target, (char *)inode->direct_block_ptr, inode->size);
        target[inode->size] = '\0';
    }
    if (!target)
        return NULL;
    struct vfs_node *node = vfs_create_symlink(name, target);
    kfree(target);
    return node;
}

void ext2_mount_directory(ext2_fs *fs, uint8_t *block_data, size_t block_size, struct vfs_node *parent) {
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
        struct vfs_node *node = type == VFS_SYMLINK ? ext2_create_symlink_node(fs, name, &child) : vfs_create_node(name, type);
        if (node) {
            node->size = child.size;
            node->inode = entry->inode;
            node->create = ext2_create;
            node->remove = ext2_remove;
            node->mkdir = ext2_mkdir;
            node->open = ext2_open;
            node->a_time = child.last_access_time;
            node->c_time = child.creation_time;
            node->m_time = child.mod_time;
            node->blocks = child.sector_count;
            node->perms = ext2_get_perms(child.type_perms);

            if (type != VFS_SYMLINK) {
                node->read = ext2_read;
                node->write = ext2_write;
                node->device = fs;
            }

            vfs_add_node(parent, node);
        }

        offset += entry->total_size;
    }
    parent->flags |= EXT2_FS_FLAGS_MOUNTED;
}

void ext2_mount(ext2_fs *fs, struct vfs_node *parent, uint32_t in) {
    ext2_inode inode;
    ext2_read_inode(fs, in, &inode);

    if (inode.size == 0) return;

    uint8_t *blocks = kmalloc(inode.size);
    ext2_read_inode_blocks(fs, &inode, blocks, 0, (inode.size + fs->block_size - 1) / fs->block_size);
    ext2_mount_directory(fs, blocks, inode.size, parent);
    kfree(blocks);
}

struct vfs_node *ext2_open(vfs_node_t *node, int flags) {
    if (node->type != VFS_DIRECTORY)
        return node;

    ext2_fs *fs = node->device;
    if (!fs)
        return NULL;
    assert(fs->sb->signature == 0xef53);

    if (!(node->flags & EXT2_FS_FLAGS_MOUNTED))
        ext2_mount(fs, node, node->inode);
    return node;
}

long mount(struct vfs_node *sda, struct vfs_node *target) {
    if (!sda)
        return -ENOENT;

    ext2_fs *fs = kmalloc(sizeof(ext2_fs));
    fs->sda = sda;
    fs->sb = (ext2_sb *)kmalloc(sizeof(ext2_sb));
    vfs_read(sda, fs->sb, 1024, sizeof(ext2_sb));

    char path[MAX_PATH]; vfs_resolve_path(path, sda);
    if (fs->sb->signature != 0xef53) {
        dprintf(LOG_ERR, "%s:%d: %s: not an ext2 partition\n", __FILE__, __LINE__, path);
        return -EINVAL;
    }
    fs->block_size = 1024 << fs->sb->log2_block;
    fs->bgd_count = (fs->sb->blocks_count / fs->sb->blocks_per_group) ?: 1;
    fs->bgd_block = fs->sb->block_num + 1;
    fs->bgd_table = (ext2_bgd *)kmalloc(fs->bgd_count * sizeof(ext2_bgd));
    fs->inode_size = fs->sb->inode_size;
    ext2_read_block(fs, fs->bgd_block, fs->bgd_table, fs->bgd_count * sizeof(ext2_bgd));
    
    target->inode = 2;
    target->device = fs;
    target->create = ext2_create;
    target->remove = ext2_remove;
    target->mkdir = ext2_mkdir;
    target->open = ext2_open;
    
    return 0;
}

int init() {
    dprintf(LOG_INFO, "%s:%d: starting ext2 driver\n", __FILE__, __LINE__);
    if (!args_contains("root")) {
        panic("root partition not specified in command line");
    }

    vfs_register("ext2", mount, false);
    return vfs_mount(vfs_open(NULL, args_value("root"), false, false), vfs_root, "ext2", 0);
}

int fini() {
    dprintf(LOG_INFO, "%s:%d: Goodbye!\n", __FILE__, __LINE__);
    return 0;
}

struct Module metadata = {
    .name = "ext2 driver",
    .init = init,
    .fini = fini
};