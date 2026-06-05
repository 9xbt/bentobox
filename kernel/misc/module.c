#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/elf64.h>
#include <kernel/zstd.h>
#include <kernel/tar.h>
#include <limine.h>
#include <zstd.h>

__attribute__((used, section(".limine_requests")))
struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

void modules_install(void) {
    if (!module_request.response)
        return;

    struct limine_file **modules = module_request.response->modules;
    for (uint64_t i = 0; i < module_request.response->module_count; i++) {
        struct limine_file *mod = modules[i];
        if (!memcmp(mod->address, "\x7f""ELF", 4)) {
            elf64_module(mod);
        } else if (*(const uint32_t *)mod->address == ZSTD_MAGICNUMBER) {
            zstd_module(mod);
        } else if (!memcmp(mod->address + 257, "ustar", 5)) {
            dprintf(LOG_INFO, "\033[93mtar:\033[0m mounting %s\n", mod->path);
            tar_module(mod->address);
        } else {
            dprintf(LOG_ERR, "\033[93m%s:\033[0m unknown file format\n", mod->path);
        }
    }
}