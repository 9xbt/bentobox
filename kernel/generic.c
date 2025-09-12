#include <kernel/module.h>
#include <kernel/sched.h>
#include <kernel/vfs.h>

extern void arch_fatal(void);

void generic_startup(void) {
    vfs_install();
    modules_install();
}

void generic_main(void) {
    sched_install();
}