#include <kernel/module.h>
#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/elf64.h>
#include <kernel/tar.h>
#include <limine.h>

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
        if (!memcmp(modules[i]->address, "\x7f""ELF", 4)) {
            elf64_module(modules[i]);
        } else if (!memcmp(modules[i]->address + 257, "ustar", 5)) {
            tar_module(modules[i]);
        } else {
            dprintf(LOG_INFO, "\033[93m%s:\033[0m unknown file format\n", modules[i]->path);
        }
    }
}