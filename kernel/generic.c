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
    dprintf(LOG_INFO, "%s:%d: running init process\n", __FILE__, __LINE__);
    char *argv[] = { "/sbin/openrc-init", "single", NULL };
    char *env[] = { "TERM=linux", NULL };
    spawn(argv[0], 0, NULL, env);
    //spawn("/bin/init", 0, NULL, NULL);
	sched_jumpstart();
}