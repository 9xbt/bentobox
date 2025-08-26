#pragma once

#define panic(fmt, ...) ({\
    __panic(__FILE__, __LINE__, fmt, ##__VA_ARGS__);\
})

void __panic(char *file, int line, char *fmt, ...);