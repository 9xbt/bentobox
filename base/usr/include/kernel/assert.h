#pragma once
#include <stddef.h>

#define assert(condition) ({\
    if (!(condition)) {\
        __assert_failed(__FILE__, __LINE__, __func__, #condition);\
    }\
    })

void __assert_failed(const char *file, size_t line, const char *func, const char *cond);