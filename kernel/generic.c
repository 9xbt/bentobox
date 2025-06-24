#include <kernel/sched.h>
#include <kernel/pci.h>
#include <kernel/vfs.h>
#include <kernel/elf64.h>
#include <kernel/printf.h>
#include <kernel/version.h>

extern void generic_load_modules(void);
extern void debugcon_entry(void);
extern void arch_redirect_logs(void);

void generic_startup(void) {
    vfs_install();
	pci_scan();
    generic_load_modules();
	sched_install();
}

void generic_main(void) {
    dprintf("%s:%d: running init process\n", __FILE__, __LINE__);
    spawn("/bin/init", 0, NULL, NULL);
    arch_redirect_logs();
	sched_start_all_cores();
}