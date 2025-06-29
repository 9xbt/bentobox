#pragma once

struct Module {
    const char *name;
    int (*init)();
    int (*fini)();
};

void load_modules();