#include <kernel/sched.h>
#include <kernel/module.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>
#include <kernel/elf64.h>
#include <kernel/printf.h>
#include <kernel/version.h>

void generic_startup(void) {
    vfs_install();
	pci_scan();
    load_modules();
	sched_install();
}

void generic_main(void) {
    dprintf("%s:%d: running init process\n", __FILE__, __LINE__);
    spawn("/bin/init", 0, NULL, NULL);
	sched_jumpstart();
}