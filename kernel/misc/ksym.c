#include <stddef.h>
#include <kernel/malloc.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/elf64.h>
#include <kernel/ksym.h>
#include <kernel/mmu.h>
#include <limine.h>

__attribute__((used, section(".limine_requests")))
struct limine_executable_file_request ksym_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST,
    .revision = 0
};

struct symbol *ksym = NULL;
static size_t ksym_used = 0, ksym_allocated = 0;

uintptr_t ksym_addr(const char *name) {
    for (size_t i = 0; i < ksym_used; i++) {
        if (!strcmp(ksym[i].name, name)) {
            return ksym[i].addr;
        }
    }
    return 0;
}

const char *ksym_name(uintptr_t addr) {
    if (addr == 0x0) {
        return "(none)";
    }
    if (addr < hhdm_offset) {
        return "(userspace)";
    }

    struct symbol *best = NULL;
    for (size_t i = 0; i < ksym_used; i++) {
        if (ksym[i].addr <= addr) {
            if (!best || ksym[i].addr > best->addr) {
                best = &ksym[i];
            }
        }
    }
    if (best) {
        uintptr_t offset = addr - best->addr;
        if (offset == 0) {
            return best->name;
        } else {
            static char buf[256] = {0};
            snprintf(buf, sizeof buf, "%s+%lu", best->name, (unsigned long)offset);
            return buf;
        }
    }
    return "(none)";
}

void ksym_expand(size_t count) {
    if (!ksym) {
        ksym_allocated = count;
        ksym = kmalloc(ksym_allocated * sizeof(struct symbol));
        return;
    }

    if ((ksym_used + count) > ksym_allocated) {
        ksym_allocated = ksym_used + count;
        ksym = krealloc(ksym, ksym_allocated * sizeof(struct symbol));
    }
}

int ksym_register(const char *name, uintptr_t addr) {
    if (!name || name[0] == '\0' || name[0] == '$' || !addr)
        return 0;
    ksym_expand(1);
    ksym[ksym_used].name = name;
    ksym[ksym_used].addr = addr;
    ksym_used++;
    return 1;
}

void ksym_install(void) {
    elf64_module(ksym_request.response->executable_file);
}