#include <kernel/vfs.h>

void generic_startup(void) {
    vfs_install();
}

void generic_main(void) {
    extern void arch_fatal();
    arch_fatal();
}