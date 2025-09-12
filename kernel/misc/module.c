#include <kernel/module.h>
#include <kernel/elf64.h>
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
        elf64_module(modules[i]);
    }
}