#include <stdint.h>
#include <errno.h>
#include <kernel/assert.h>
#include <kernel/malloc.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/args.h>
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
    vfs_node_t *sda;
    ext2_sb    *sb;
    ext2_bgd   *bgd_table;
    uint32_t block_size;
    uint32_t bgd_count;
    uint32_t bgd_block;
    uint32_t inode_size;
} ext2_fs;

void ext2_mount(ext2_fs *fs, struct vfs_node *parent, uint32_t in);

void ext2_read_block(ext2_fs *fs, uint32_t block, void *buffer, uint32_t count) {
    vfs_read(fs->sda, buffer, block * fs->block_size, count);
}

void ext2_read_inode(ext2_fs *fs, uint32_t inode, ext2_inode *in) {
    inode--;
    uint32_t block_group = inode / fs->sb->inodes_per_group;
    uint32_t inode_index = inode % fs->sb->inodes_per_group;
    uint32_t inode_block = (inode_index * fs->inode_size) / fs->block_size;

    uint8_t buffer[fs->block_size];
    ext2_read_block(fs, fs->bgd_table[block_group].inode_table_block + inode_block, buffer, fs->block_size);
    memcpy(in, buffer + (inode_index % (fs->block_size / fs->inode_size)) * fs->inode_size, fs->inode_size);
}

void ext2_read_direct_blocks(ext2_fs *fs, uint32_t *blocks, void *buffer, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (blocks[i]) {
            ext2_read_block(fs, blocks[i], (uint8_t*)buffer + (i * fs->block_size), fs->block_size);
        }
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

    uint32_t i;
    for (i = 0; i < count; i++) {
        if (block_ptrs[offset + i]) {
            ext2_read_block(fs, block_ptrs[offset + i], buffer + (i * fs->block_size), fs->block_size);
        }
    }

    kfree(block_ptrs);
    return i;
}

uint32_t ext2_read_doubly_blocks(ext2_fs *fs, uint32_t block, uint8_t *buffer, uint32_t offset, uint32_t count) {
    if (!block)
        return 0;

    uint32_t ptrs_per_block = fs->block_size / sizeof(uint32_t);
    uint32_t *doubly_ptrs = kmalloc(fs->block_size);
    ext2_read_block(fs, block, doubly_ptrs, fs->block_size);

    uint32_t read = 0;
    for (uint32_t i = offset / ptrs_per_block; i < ptrs_per_block && read < count; i++) {
        uint32_t singly_offset = (i == offset / ptrs_per_block) ? offset % ptrs_per_block : 0;
        uint32_t singly_count = (count - read > ptrs_per_block - singly_offset) ? ptrs_per_block - singly_offset : count - read;
        
        if (doubly_ptrs[i])
            read += ext2_read_singly_blocks(fs, doubly_ptrs[i], buffer + read * fs->block_size, singly_offset, singly_count);
        else
            read += singly_count;
    }

    kfree(doubly_ptrs);
    return read;
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

long ext2_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    ext2_fs *fs = node->device;
    if (!fs)
        return -ENOENT;
    if (fs->sb->signature != 0xef53)
        return -EIO;
    
    ext2_inode inode;
    ext2_read_inode(fs, node->inode, &inode);

    if (offset >= inode.size)
        return 0;
    if (offset + len > inode.size)
        len = inode.size - offset;

    uint32_t block = offset / fs->block_size;
    uint32_t count = ((offset + len - 1) / fs->block_size) - block + 1;

    uint8_t *buf = kmalloc(count * fs->block_size);
    ext2_read_inode_blocks(fs, &inode, buf, block, count);
    memcpy(buffer, buf + (offset % fs->block_size), len);

    kfree(buf);
    return len;
}

long ext2_write(struct vfs_node *node, void *buffer, long offset, size_t len) {
    unimplemented;
    return -1;
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
            node->driver = VFS_DRIVER_EXT2;

            if (type != VFS_SYMLINK) {
                node->read = ext2_read;
                node->write = ext2_write;
                node->device = fs;
            }

            vfs_add_node(parent, node);
            if (node->type == VFS_DIRECTORY &&
                strcmp(name, ".") && 
                strcmp(name, "..")) {
                ext2_mount(fs, node, entry->inode);
            }
        }

        offset += entry->total_size;
    }
}

void ext2_mount(ext2_fs *fs, struct vfs_node *parent, uint32_t in) {
    ext2_inode inode;
    ext2_read_inode(fs, in, &inode);

    uint32_t total_blocks = (inode.size + fs->block_size - 1) / fs->block_size;

    for (uint32_t i = 0; i < total_blocks && i < 12; i++) {
        if (inode.direct_block_ptr[i] == 0)
            continue;

        uint8_t *block = kmalloc(fs->block_size);
        ext2_read_block(fs, inode.direct_block_ptr[i], block, fs->block_size);

        ext2_mount_directory(fs, block, fs->block_size, parent);
        kfree(block);
    }
}

int init() {
    dprintf("%s:%d: starting ext2 driver\n", __FILE__, __LINE__);

    if (!args_contains("root")) {
        dprintf("%s:%d: root partition not specified in command line\n", __FILE__, __LINE__);
        return 1;
    }

    struct vfs_node *sda = vfs_open(NULL, args_value("root"), false, false);
    if (!sda) {
        dprintf("%s:%d: cannot open %s\n", __FILE__, __LINE__, args_value("root"));
        return 1;
    }

    ext2_fs *fs = kmalloc(sizeof(ext2_fs));
    fs->sda = sda;
    fs->sb = (ext2_sb *)kmalloc(sizeof(ext2_sb));
    vfs_read(sda, fs->sb, 1024, sizeof(ext2_sb));

    if (fs->sb->signature != 0xef53) {
        dprintf("%s:%d: %s: not an ext2 partition\n", __FILE__, __LINE__, args_value("root"));
        return 1;
    }
    fs->block_size = 1024 << fs->sb->log2_block;
    fs->bgd_count = (fs->sb->blocks_count / fs->sb->blocks_per_group) ?: 1;
    fs->bgd_block = fs->sb->block_num + 1;
    fs->bgd_table = (ext2_bgd *)kmalloc(fs->bgd_count * sizeof(ext2_bgd));
    fs->inode_size = fs->sb->inode_size;
    ext2_read_block(fs, fs->bgd_block, fs->bgd_table, fs->bgd_count * sizeof(ext2_bgd));
    
    vfs_root->inode = 2;
    vfs_root->device = fs;
    ext2_mount(fs, vfs_root, 2);

    return 0;
}

int fini() {
    dprintf("%s:%d: Goodbye!\n", __FILE__, __LINE__);
    return 0;
}

struct Module metadata = {
    .name = "ext2 driver",
    .init = init,
    .fini = fini
};