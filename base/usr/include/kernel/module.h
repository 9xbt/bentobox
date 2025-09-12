#pragma once

struct Module {
    const char *name;
    int (*init)();
    int (*fini)();
};

void modules_install(void);