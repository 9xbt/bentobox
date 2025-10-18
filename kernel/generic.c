#include <kernel/module.h>
#include <kernel/sched.h>
#include <kernel/elf64.h>
#include <kernel/time.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>

extern void arch_jumpstart(void);

void generic_startup(void) {
    arch_clock_init();
    vfs_install();
    pci_scan();
    modules_install();
    sched_install();
}

void generic_main(void) {
    spawn("/bin/init", 0, NULL, NULL);
    arch_jumpstart();
}