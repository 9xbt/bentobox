#include <kernel/version.h>
#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>

void generic_startup(void) {
    vfs_install();
	pci_scan();
    load_modules();
	sched_install();
}

void generic_main(void) {
    dprintf(6, "%s:%d: running init process\n", __FILE__, __LINE__);
    spawn("/bin/init", 0, NULL, NULL);
    asm volatile ("int3");
	sched_jumpstart();
}