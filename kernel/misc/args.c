#include <stddef.h>
#include <kernel/string.h>
#include <limine.h>

const char *cmdline = NULL;

__attribute__((used, section(".limine_requests")))
struct limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST,
    .revision = 0
};

void args_cache(void) {
    if (!cmdline)
        cmdline = cmdline_request.response->cmdline;
}

int args_contains(const char *s) {
    args_cache();
    return strstr(cmdline, s) != NULL;
}

char *args_value(const char *s) {
    args_cache();
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