#include <errno.h>
#include <stdint.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>
#include <kernel/malloc.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>

#define EXT_FIFO        0x1000
#define EXT_CHAR_DEV    0x2000
#define EXT_DIRECTORY   0x4000
#define EXT_BLOCK_DEV   0x6000
#define EXT_FILE        0x8000
#define EXT_SYM_LINK    0xA000
#define EXT_UNIX_SOCKET 0xC000

#define EXT_MAX_CACHE   0x1024

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
    uint8_t  unused_ext[787];
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
    uint8_t  name[];
} ext2_dirent;

typedef struct {
    uint32_t bitmap_block;
    uint32_t bitmap_inode;
    uint32_t inode_table_block;
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t directories_count;
    uint16_t pad;
    uint8_t  resv[12];
} ext2_bgd;

typedef struct {
    ext2_sb    *sb;
    ext2_bgd   *bgd_table;
    ext2_inode *root_inode;
    uint32_t block_size;
    uint32_t bgd_count;
    uint32_t bgd_block;
    uint32_t inode_size;
} ext2_fs;

typedef struct {
    uint32_t block_num;
    uint8_t *data;
    bool valid;
    uint32_t access_time;
} block_cache_entry;

ext2_fs ext2fs;
struct vfs_node *hda = NULL;
static block_cache_entry cache[EXT_MAX_CACHE];
static uint32_t cache_counter = 0;

void ext2_cache_init() {
    memset(cache, 0, sizeof(cache));
}

void ext2_cache_free() {
    for (int i = 0; i < EXT_MAX_CACHE; i++) {
        if (cache[i].data) {
            kfree(cache[i].data);
            cache[i].data = NULL;
        }
    }
}

static int find_cache_slot(uint32_t block) {
    int lru_idx = 0;
    
    for (int i = 0; i < EXT_MAX_CACHE; i++) {
        if (cache[i].valid && cache[i].block_num == block) {
            cache[i].access_time = ++cache_counter;
            return i;
        }
        if (!cache[i].valid || cache[i].access_time < cache[lru_idx].access_time) {
            lru_idx = i;
        }
    }
    return -(lru_idx + 1);
}

void ext2_read_block(ext2_fs *fs, uint32_t block, void* buf, uint32_t count) {
    int slot = find_cache_slot(block);
    
    if (slot >= 0) {
        memcpy(buf, cache[slot].data, count);
        return;
    }
    
    slot = -(slot + 1);
    if (!cache[slot].data) {
        cache[slot].data = kmalloc(fs->block_size);
    }
    
    vfs_read(hda, cache[slot].data, block * fs->block_size, fs->block_size);
    memcpy(buf, cache[slot].data, count);
    
    cache[slot].block_num = block;
    cache[slot].valid = true;
    cache[slot].access_time = ++cache_counter;
}

void ext2_read_block_range(ext2_fs *fs, uint32_t *blocks, uint32_t count, void* buf) {
    if (count == 0) return;
    
    bool sequential = true;
    for (uint32_t i = 1; i < count && sequential; i++) {
        sequential = (blocks[i] != 0 && blocks[i] == blocks[i-1] + 1);
    }
    
    if (sequential && count > 1) {
        vfs_read(hda, buf, blocks[0] * fs->block_size, count * fs->block_size);
        
        for (uint32_t i = 0; i < count; i++) {
            int slot = find_cache_slot(blocks[i]);
            if (slot < 0) slot = -(slot + 1);
            
            if (!cache[slot].data) {
                cache[slot].data = kmalloc(fs->block_size);
            }
            memcpy(cache[slot].data, (uint8_t*)buf + (i * fs->block_size), fs->block_size);
            cache[slot].block_num = blocks[i];
            cache[slot].valid = true;
            cache[slot].access_time = ++cache_counter;
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            if (blocks[i]) {
                ext2_read_block(fs, blocks[i], (uint8_t*)buf + (i * fs->block_size), fs->block_size);
            }
        }
    }
}

void ext2_read_inode(ext2_fs *fs, uint32_t inode, ext2_inode *in) {
    uint32_t bg = (inode - 1) / fs->sb->inodes_per_group;
    uint32_t idx = (inode - 1) % fs->sb->inodes_per_group;
    uint32_t bg_idx = (idx * fs->inode_size) / fs->block_size;

    char buf[fs->block_size];
    ext2_read_block(fs, fs->bgd_table[bg].inode_table_block + bg_idx, buf, fs->block_size);
    memcpy(in, (buf + (idx % (fs->block_size / fs->inode_size)) * fs->inode_size), fs->inode_size);
}

uint32_t ext2_read_singly_blocks(ext2_fs *fs, uint32_t block, uint8_t *buf, uint32_t offset, uint32_t count) {
    uint32_t *block_ptrs = (uint32_t *)kmalloc(fs->block_size);
    ext2_read_block(fs, block, block_ptrs, fs->block_size);
    
    uint32_t start_idx = offset / fs->block_size;
    uint32_t start_offset = offset % fs->block_size;
    uint32_t max_blocks = fs->block_size / 4;

    uint32_t valid_count = 0;
    while (start_idx + valid_count < max_blocks && 
        block_ptrs[start_idx + valid_count] != 0 &&
        valid_count * fs->block_size < count + start_offset) {
        valid_count++;
    }
    
    uint32_t bytes_read = 0;
    if (valid_count > 0) {
        uint8_t *buffer = (uint8_t *)kmalloc(valid_count * fs->block_size);
        ext2_read_block_range(fs, &block_ptrs[start_idx], valid_count, buffer);
        
        uint32_t available = valid_count * fs->block_size - start_offset;
        bytes_read = (count < available) ? count : available;
        memcpy(buf, buffer + start_offset, bytes_read);
        
        kfree(buffer);
    }
    
    kfree(block_ptrs);
    return bytes_read;
}

uint32_t ext2_read_doubly_blocks(ext2_fs *fs, uint32_t doubly_block_id, uint8_t *buf, uint32_t offset, uint32_t count) {
    uint32_t *singly_ptrs = (uint32_t*)kmalloc(fs->block_size);
    ext2_read_block(fs, doubly_block_id, singly_ptrs, fs->block_size);
    
    uint32_t blocks_per_singly = fs->block_size / 4;
    uint32_t bytes_per_singly = blocks_per_singly * fs->block_size;
    uint32_t start_singly = offset / bytes_per_singly;
    uint32_t singly_offset = offset % bytes_per_singly;
    
    uint32_t bytes_read = 0;
    uint32_t remaining = count;
    
    for (uint32_t i = start_singly; i < blocks_per_singly && remaining > 0 && singly_ptrs[i] != 0; i++) {
        uint32_t n = (remaining > bytes_per_singly - singly_offset) ? (bytes_per_singly - singly_offset) : remaining,
              read = ext2_read_singly_blocks(fs, singly_ptrs[i], buf + bytes_read, singly_offset, n);
        bytes_read += read;
        remaining -= read;
        singly_offset = 0;
        
        if (read < n) break;
    }
    
    kfree(singly_ptrs);
    return bytes_read;
}

void ext2_read_inode_blocks_range(ext2_fs *fs, ext2_inode *in, uint8_t *buf, uint32_t start_block, uint32_t block_count) {
    uint32_t blocks_per_singly = fs->block_size / 4;
    uint32_t blocks_per_doubly = blocks_per_singly * blocks_per_singly;
    
    uint32_t buf_offset = 0;
    uint32_t current_block = start_block;
    uint32_t remaining = block_count;
    
    /* Direct blocks */
    if (current_block < 12 && remaining > 0) {
        uint32_t end_block = (current_block + remaining > 12) ? 12 : current_block + remaining;
        uint32_t count = end_block - current_block;
        
        // Read sequential direct blocks
        ext2_read_block_range(fs, &in->direct_block_ptr[current_block], count, buf + buf_offset);
        
        buf_offset += count * fs->block_size;
        current_block += count;
        remaining -= count;
    }
    
    /* Indirect blocks */
    uint32_t singly_end = 12 + blocks_per_singly;
    if (current_block < singly_end && remaining > 0 && in->singly_block_ptr != 0) {
        uint32_t singly_offset = (current_block >= 12) ? current_block - 12 : 0;
        uint32_t count = (current_block + remaining > singly_end) ? singly_end - current_block : remaining;
        
        uint32_t bytes_read = ext2_read_singly_blocks(
            fs, in->singly_block_ptr, 
            buf + buf_offset, 
            singly_offset * fs->block_size, 
            count * fs->block_size);
        
        uint32_t blocks_read = bytes_read / fs->block_size;
        buf_offset += bytes_read;
        current_block += blocks_read;
        remaining -= blocks_read;
        
        if (blocks_read < count) return;
    }
    
    /* Doubly indirect blocks */
    uint32_t doubly_end = 12 + blocks_per_singly + blocks_per_doubly;
    if (current_block < doubly_end && remaining > 0 && in->doubly_block_ptr != 0) {
        uint32_t doubly_offset = (current_block >= singly_end) ? current_block - singly_end : 0;
        
        ext2_read_doubly_blocks(fs, in->doubly_block_ptr, 
                               buf + buf_offset, 
                               doubly_offset * fs->block_size, 
                               remaining * fs->block_size);
    }
}

void ext2_read_inode_blocks(ext2_fs *fs, ext2_inode *in, uint8_t *buf, uint32_t count) {
    uint32_t blocks = DIV_CEILING(count, fs->block_size);
    ext2_read_inode_blocks_range(fs, in, buf, 0, blocks);
}

long ext2_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    uint8_t in[ext2fs.inode_size];
    ext2_inode *inode = (ext2_inode *)in;
    ext2_read_inode(&ext2fs, node->inode, inode);

    if (offset >= inode->size) {
        return 0;
    }
    if (offset + len > inode->size) {
        len = inode->size - offset;
    }

    uint32_t start_block = offset / ext2fs.block_size;
    uint32_t end_block = (offset + len - 1) / ext2fs.block_size;
    uint32_t blocks_needed = end_block - start_block + 1;
    
    uint8_t *buf = (uint8_t *)kmalloc(blocks_needed * ext2fs.block_size);
    ext2_read_inode_blocks_range(&ext2fs, inode, buf, start_block, blocks_needed);
    
    uint32_t block_offset = offset % ext2fs.block_size;
    memcpy(buffer, buf + block_offset, len);

    kfree(buf);
    return len;
}

struct vfs_node *ext2_create_symlink_node(ext2_fs *fs, const char *name, ext2_inode *inode) {
    char *target = NULL;

    if (inode->size <= 60) {
        /* Fast symlink */
        target = kmalloc(inode->size + 1);
        memcpy(target, (char *)inode->direct_block_ptr, inode->size);
        target[inode->size] = '\0';
    } else {
        /* Slow symlink */
        if (inode->direct_block_ptr[0] != 0) {
            target = kmalloc(inode->size + 1);
            uint8_t *block_data = kmalloc(fs->block_size);
            
            ext2_read_block(fs, inode->direct_block_ptr[0], block_data, fs->block_size);
            memcpy(target, block_data, inode->size);
            target[inode->size] = '\0';
            
            kfree(block_data);
        }
    }
    
    if (!target) return NULL;
    
    struct vfs_node *node = vfs_create_symlink(name, target);
    kfree(target);
    return node;
}

void ext2_mount(ext2_fs *fs, struct vfs_node *parent, uint32_t inode_num) {
    uint8_t in[ext2fs.inode_size];
    ext2_inode *inode = (ext2_inode *)in;
    ext2_read_inode(fs, inode_num, inode);
    
    uint32_t block_size = fs->block_size;
    uint32_t total_blocks = DIV_CEILING(inode->size, block_size);
    
    uint32_t *dir_blocks = (uint32_t *)kmalloc(12 * sizeof(uint32_t));
    uint32_t valid_blocks = 0;
    
    for (unsigned i = 0; i < 12 && i < total_blocks; i++) {
        if (inode->direct_block_ptr[i] != 0) {
            dir_blocks[valid_blocks++] = inode->direct_block_ptr[i];
        }
    }
    
    if (valid_blocks > 0) {
        uint8_t *all_blocks = (uint8_t *)kmalloc(valid_blocks * block_size);
        ext2_read_block_range(fs, dir_blocks, valid_blocks, all_blocks);
        
        for (uint32_t block_idx = 0; block_idx < valid_blocks; block_idx++) {
            uint8_t *block = all_blocks + (block_idx * block_size);
            uint32_t offset = 0;
            
            while (offset < block_size) {
                ext2_dirent *entry = (ext2_dirent *)(block + offset);
                if (entry->inode == 0 || entry->total_size == 0)
                    break;

                char name[256];
                memcpy(name, entry->name, entry->name_len);
                name[entry->name_len] = '\0';

                ext2_inode *child = (ext2_inode *)kmalloc(fs->inode_size);
                ext2_read_inode(fs, entry->inode, child);

                uint16_t type = child->type_perms & 0xF000;
                uint32_t vfs_type = VFS_NONE;
                struct vfs_node *node = NULL;

                switch (type) {
                    case EXT_FILE:
                        vfs_type = VFS_FILE;
                        node = vfs_create_node(name, vfs_type);
                        break;
                    case EXT_DIRECTORY:
                        vfs_type = VFS_DIRECTORY;
                        node = vfs_create_node(name, vfs_type);
                        break;
                    case EXT_CHAR_DEV:
                        vfs_type = VFS_CHARDEVICE;
                        node = vfs_create_node(name, vfs_type);
                        break;
                    case EXT_BLOCK_DEV:
                        vfs_type = VFS_BLOCKDEVICE;
                        node = vfs_create_node(name, vfs_type);
                        break;
                    case EXT_SYM_LINK:
                        node = ext2_create_symlink_node(fs, name, child);
                        break;
                }

                if (node) {
                    node->size = child->size;
                    node->inode = entry->inode;
                    node->driver = VFS_DRIVER_EXT2;
                    
                    if (type != EXT_SYM_LINK) {
                        node->read = ext2_read;
                    }
                    
                    vfs_add_node(parent, node);

                    if (type == EXT_DIRECTORY && strcmp(name, "lost+found") != 0 && 
                        !(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)) {
                        ext2_mount(fs, node, entry->inode);
                    }
                }

                offset += entry->total_size;
                kfree(child);
            }
        }
        
        kfree(all_blocks);
    }
    
    kfree(dir_blocks);
}

int init() {
    dprintf("%s:%d: starting ext2 driver\n", __FILE__, __LINE__);

    ext2_cache_init();

    //hda = vfs_open(NULL, "/dev/hda");
    hda = vfs_open(NULL, "/dev/sda", false, false);

    ext2_sb *sb = (ext2_sb *)kmalloc(512);
    vfs_read(hda, (void *)sb, 1024, sizeof(ext2_sb));

    if (sb->signature != 0xef53) {
        dprintf("%s:%d: not an ext2 partition\n", __FILE__, __LINE__);
        ext2_cache_free();
        return -EINVAL;
    }

    ext2fs.sb = sb;
    ext2fs.block_size = 1024 << sb->log2_block;
    ext2fs.bgd_count = sb->blocks_count / sb->blocks_per_group;
    if (!ext2fs.bgd_count) {
        ext2fs.bgd_count = 1;
    }
    ext2fs.bgd_block = sb->block_num + 1;
    ext2fs.bgd_table = (ext2_bgd *)kmalloc(ext2fs.bgd_count * sizeof(ext2_bgd));
    ext2_read_block(&ext2fs, ext2fs.bgd_block, ext2fs.bgd_table, ext2fs.bgd_count * sizeof(ext2_bgd));
    ext2fs.inode_size = sb->inode_size;

    ext2fs.root_inode = (ext2_inode *)kmalloc(ext2fs.inode_size);
    ext2_read_inode(&ext2fs, 2, ext2fs.root_inode);

    dprintf("%s:%d: mounting /dev/%s to /\n", __FILE__, __LINE__, hda->name);
    ext2_mount(&ext2fs, vfs_root, 2);
    //printf("\033[92m * \033[97mInitialized ext2fs\033[0m\n");
    return 0;
}

int fini() {
    dprintf("%s:%d: Goodbye!\n", __FILE__, __LINE__);
    ext2_cache_free();
    kfree(ext2fs.sb);
    kfree(ext2fs.bgd_table);
    return 0;
}

struct Module metadata = {
    .name = "ext2 driver",
    .init = init,
    .fini = fini
};