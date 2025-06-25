#include <kernel/arch/x86_64/gdt.h>
#include <kernel/arch/x86_64/tss.h>
#include <kernel/arch/x86_64/idt.h>
#include <kernel/arch/x86_64/smp.h>
#include <kernel/arch/x86_64/ps2.h>
#include <kernel/arch/x86_64/vga.h>
#include <kernel/arch/x86_64/tsc.h>
#include <kernel/arch/x86_64/hpet.h>
#include <kernel/arch/x86_64/user.h>
#include <kernel/arch/x86_64/lapic.h>
#include <kernel/arch/x86_64/ioapic.h>
#include <kernel/arch/x86_64/serial.h>
#include <kernel/mmu.h>
#include <kernel/pci.h>
#include <kernel/lfbvideo.h>
#include <kernel/acpi.h>
#include <kernel/args.h>
#include <kernel/panic.h>
#include <kernel/elf64.h>
#include <kernel/sched.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/assert.h>
#include <kernel/version.h>
#include <kernel/flanterm.h>
#include <kernel/spinlock.h>
#include <kernel/multiboot.h>

extern void generic_startup(void);
extern void generic_main(void);
extern void arch_redirect_logs(void);

void *mboot = NULL;

atomic_flag flanterm_lock = ATOMIC_FLAG_INIT;

void *mboot2_find_next(char *current, uint32_t type) {
	char *header = current;
	while ((uintptr_t)header & 7) header++;
	struct multiboot_tag *tag = (void *)header;
	while (1) {
		if (tag->type == 0) return NULL;
		if (tag->type == type) return tag;

		header += tag->size;
		while ((uintptr_t)header & 7) header++;
		tag = (void*)header;
	}
}

void *mboot2_find_tag(void *base, uint32_t type) {
	char *header = base;
	header += 8;
	return mboot2_find_next(header, type);
}

void mboot2_load_modules(void *base) {
	struct multiboot_tag_module *mod = mboot2_find_tag(base, MULTIBOOT_TAG_TYPE_MODULE);
    while (mod) {
		if (strcmp(mod->string, "ksym")) elf_module(mod);
        mod = mboot2_find_next((char *)mod + ALIGN_UP(mod->size, 8), MULTIBOOT_TAG_TYPE_MODULE);
    }
}

void putchar(char c) {
	if (!ft_ctx) {
		vga_putchar(c);
	} else {
		acquire(&flanterm_lock);
		flanterm_write(ft_ctx, &c, 1);
		release(&flanterm_lock);
	}
}

void puts(char *s) {
	if (!ft_ctx) {
		vga_puts(s);
	} else {
		acquire(&flanterm_lock);
		flanterm_write(ft_ctx, s, strlen(s));
		release(&flanterm_lock);
	}
}

void arch_prepare_fatal(void) {
	arch_redirect_logs();
	for (uint32_t i = 0; i < madt_lapics; i++) {
		if (i == this_core()->lapic_id) continue;
		lapic_ipi(i, 0x447D);
	}
}

void arch_fatal(void) {
	asm ("cli");
	for (;;) asm ("hlt");
}

const char *arch_get_cmdline(void) {
	struct multiboot_tag_string *s = mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_CMDLINE);
	if (s) {
		return s->string;
	}
	return "";
}

void generic_load_modules(void) {
	assert(mboot);
	mboot2_load_modules(mboot);
}

void mubsan_log(const char* fmt, ...) {
	arch_prepare_fatal();

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

	arch_fatal();
}

void kmain(void *mboot_info, uint32_t mboot_magic) {
    vga_clear();
    serial_install();
    
    dprintf("%s %d.%d.%d %s %s %s %s\n",
        __kernel_name, __kernel_version_major, __kernel_version_minor, __kernel_version_patch,
		__kernel_commit_hash, __kernel_build_date, __kernel_build_time, __kernel_arch);

    assert(mboot_magic == 0x36d76289);
	mboot = mboot_info;
	cmdline = arch_get_cmdline();

    gdt_install();
    idt_install();
	pmm_install();
	tss_install();
	vmm_install();
	extern void malloc_initialize();
	malloc_initialize();
	//create_kernel_heap(); // TODO: fancy this
	framebuffer_initialize();

	//printf("\n  \033[97mStarting up \033[94mbentobox (%s)\033[0m\n\n", __kernel_arch);

	elf_module(mboot2_find_tag(mboot, MULTIBOOT_TAG_TYPE_MODULE));
	acpi_install();
	lapic_install();
	ioapic_install();
	ps2_install();
	hpet_install();
	lapic_calibrate_timer();
	smp_initialize();
	user_initialize();

	generic_startup();
	generic_main();
}