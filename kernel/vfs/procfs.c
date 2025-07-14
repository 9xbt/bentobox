#include <stdbool.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

long procfs_meminfo_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    if (offset != 0) return 0;
    char buf[1024];
    int i = sprintf(buf, ""
        "MemTotal: %lu kB\n"
        "MemFree: %lu kB\n"
        "Buffers: 0 kB\n"
        "Cached: 0 kB\n"
        "SwapCached: 0 kB\n"
        "SwapTotal: 0 kB\n"
        "SwapFree: 0 kB\n",
        mmu_usable_mem / 1024,
        mmu_usable_mem / 1024 - mmu_used_pages * 4
    );
    strncpy(buffer, buf, len);
    return i;
}

void procfs_initialize(void) {
    struct vfs_node *proc = vfs_create_node("proc", VFS_DIRECTORY);
    vfs_add_node(NULL, proc);

    struct vfs_node *meminfo = vfs_create_node("meminfo", VFS_CHARDEVICE);
    meminfo->perms = 0444;
    meminfo->read = procfs_meminfo_read;
    vfs_add_node(proc, meminfo);
}