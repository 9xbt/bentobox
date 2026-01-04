#include <kernel/ringbuffer.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/mmu.h>
#include <kernel/vfs.h>

vfs_node_t *procfs_create(vfs_node_t *parent, const char *name, vfs_node_type_t type);

long meminfo_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;

    char buf[1024];
    size_t i = snprintf(buf, sizeof buf, ""
        "MemTotal:      %lu kB\n"
        "MemFree:       %lu kB\n"
        "MemAvailable:  %lu kB\n",
        mmu_usable_mem / 1024,
        mmu_usable_mem / 1024 - mmu_used_pages * 4,
        mmu_usable_mem / 1024 - mmu_used_pages * 4
    );

    if ((size_t)offset > i)
        return 0;

    size_t n = len < i - offset ? len : i - offset;
    memcpy(buffer, buf + offset, len > i ? i : len);
    return n;
}

long uptime_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;

    size_t secs = 0, nanos = 0;
    uptime(&secs, &nanos);

    char buf[1024];
    size_t i = snprintf(buf, sizeof buf, "%lu.%02lu %lu.%02lu\n", secs, nanos / 10000000, 0, 0);

    if ((size_t)offset > i)
        return 0;

    size_t n = len < i - offset ? len : i - offset;
    memcpy(buffer, buf + offset, len > i ? i : len);
    return n;
}

long kmsg_read(vfs_node_t *node, void *buffer, long offset, size_t len) {
    (void)node;
    return ringbuffer_peek(kernel_rb, buffer, len, (size_t)offset);
}

vfs_ops_t meminfo_ops = {
    .read = meminfo_read
};

vfs_ops_t uptime_ops = {
    .read = uptime_read
};

vfs_ops_t kmsg_ops = {
    .read = kmsg_read
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

long procfs_mount(vfs_node_t *node, vfs_node_t *device, long flags) {
    (void)device;
    (void)flags;
    node->ops = &procfs_ops;

    vfs_node_t *meminfo = vfs_create_node("meminfo", VFS_FILE);
    meminfo->perms = 0444;
    meminfo->ops = &meminfo_ops;
    vfs_add_node(node, meminfo);

    vfs_node_t *uptime = vfs_create_node("uptime", VFS_FILE);
    uptime->perms = 0444;
    uptime->ops = &uptime_ops;
    vfs_add_node(node, uptime);

    vfs_node_t *kmsg = vfs_create_node("kmsg", VFS_FILE);
    kmsg->perms = 0444;
    kmsg->ops = &kmsg_ops;
    vfs_add_node(node, kmsg);
    
    return 0;
}

vfs_mount_ops_t procfs_mount_ops = {
    .type  = "proc",
    .nodev = true,
    .mount = procfs_mount
};

void procfs_initialize(void) {
    vfs_register(&procfs_mount_ops);
}