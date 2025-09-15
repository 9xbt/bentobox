#include <kernel/module.h>
#include <kernel/sched.h>
#include <kernel/vfs.h>

extern void arch_jumpstart(void);

void generic_startup(void) {
    vfs_install();
    modules_install();
    sched_install();
}

void generic_main(void) {
    arch_jumpstart();
}