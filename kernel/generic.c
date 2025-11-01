#include <kernel/module.h>
#include <kernel/bitmap.h>
#include <kernel/sched.h>
#include <kernel/elf64.h>
#include <kernel/panic.h>
#include <kernel/time.h>
#include <kernel/pci.h>
#include <kernel/tty.h>
#include <kernel/vfs.h>

extern void sched_free_pid(int pid);
extern void arch_jumpstart(void);

void generic_startup(void) {
    arch_clock_init();
    vfs_install();
    pci_scan();
    modules_install();
    sched_install();
    tty_spawn_worker();
}

void generic_main(void) {
    sched_free_pid(1);
    if (spawn("/bin/init", 0, NULL, NULL) < 0)
        panic("Failed to spawn init process!");
    arch_jumpstart();
}