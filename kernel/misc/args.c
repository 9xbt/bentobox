#include <stddef.h>
#include <kernel/string.h>

const char *cmdline = NULL;

int args_contains(const char *s) {
    if (!cmdline) return 0;
    return strstr(cmdline, s) != NULL;
}

char *args_value(const char *s) {
    if (!cmdline) return NULL;
    char *arg = strstr(cmdline, s);
    if (!arg)
        return NULL;
    arg += strlen(s);
    if (*arg != '=')
        return NULL;
    arg++;
    
    char *end = strchr(arg, ' ');
    size_t len = end ? (size_t)(end - arg) : strlen(arg);
    
    static char result[256];
    strncpy(result, arg, len);
    result[len] = '\0';
    
    return result;
}