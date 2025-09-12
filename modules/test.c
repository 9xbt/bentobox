#include <kernel/module.h>
#include <kernel/printf.h>

int init() {
    dprintf(LOG_INFO, "Hello, module world!\n");
    return 0;
}

int fini() {
    return 0;
}

struct Module metadata = {
    .name = "test module",
    .init = init,
    .fini = fini
};