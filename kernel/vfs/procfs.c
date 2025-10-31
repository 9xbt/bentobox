#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

vfs_node_t *procfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);

long meminfo_read(struct vfs_node *node, void *buffer, long offset, size_t len) {
    (void)node;

    char buf[1024];
    size_t i = snprintf(buf, sizeof buf, ""
        "MemTotal: %lu kB\n"
        "MemFree:  %lu kB\n",
        mmu_usable_mem / 1024,
        mmu_usable_mem / 1024 - mmu_used_pages * 4
    );

    if ((size_t)offset > i)
        return 0;

    size_t n = len < i - offset ? len : i - offset;
    memcpy(buffer, buf + offset, len > i ? i : len);
    return n;
}

vfs_ops_t meminfo_ops = {
    .read = meminfo_read
};

vfs_ops_t procfs_ops = {
    .create = procfs_create
};

vfs_node_t *procfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type) {
    vfs_node_t *node = vfs_create_node(name, type);
    node->size = 0;
    node->ops = &procfs_ops;
    vfs_add_node(parent, node);
    return node;
}

void procfs_initialize(void) {
    vfs_node_t *proc = vfs_create_node("proc", VFS_DIRECTORY);
    proc->ops = &procfs_ops;
    vfs_add_node(NULL, proc);

    vfs_node_t *meminfo = vfs_create_node("meminfo", VFS_FILE);
    meminfo->perms = 0444;
    meminfo->ops = &meminfo_ops;
    vfs_add_node(proc, meminfo);
}