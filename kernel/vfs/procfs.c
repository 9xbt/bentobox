#include <stdbool.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

long procfs_meminfo_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (offset != 0)
        return 0;
    char buf[1024];
    int i = sprintf(buf, ""
        "MemTotal: %lu kB\n"
        "MemFree:  %lu kB\n",
        mmu_usable_mem / 1024,
        mmu_usable_mem / 1024 - mmu_used_pages * 4
    );
    strncpy(buffer, buf, len);
    return i;
}

long procfs_filesystems_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (offset != 0)
        return 0;
    char buf[1024];
    int i = sprintf(buf, ""
        "nodev   tmpfs\n"
        "nodev   proc\n"
        "        ext2\n"
    );
    strncpy(buffer, buf, len);
    return i;
}

long procfs_mount(struct vfs_node *source, struct vfs_node *proc) {
    (void)source;

    struct vfs_node *meminfo = vfs_create_node("meminfo", VFS_FILE);
    meminfo->perms = 0444;
    meminfo->read = procfs_meminfo_read;
    vfs_add_node(proc, meminfo);

    struct vfs_node *mounts = vfs_create_node("mounts", VFS_FILE);
    mounts->perms = 0444;
    vfs_add_node(proc, mounts);

    struct vfs_node *filesystems = vfs_create_node("filesystems", VFS_FILE);
    filesystems->perms = 0444;
    filesystems->read = procfs_filesystems_read;
    vfs_add_node(proc, filesystems);
    return 0;
}

void procfs_initialize(void) {
    vfs_register("proc", procfs_mount);
}