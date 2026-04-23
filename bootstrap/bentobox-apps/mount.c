#include <abi-bits/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

char *name;

void mount(const char *path, const char *type, const char *device, long flags) {
    if (access(path, F_OK)) {
        fprintf(stderr, "%s: ", name);
        perror(path);
        exit(EXIT_FAILURE);
    }

    errno = -__syscall4(SYS_mount, (long)path, (long)type, (long)device, flags);
    if (errno) {
        perror(name);
        exit(EXIT_FAILURE);
    }
}

void print_usage(void) {
    printf(
        "Usage: %s [OPTIONS...] DEVICE TYPE DIRECTORY\n"
        "Mount a filesystem\n"
        "\n"
        "Options:\n"
        "     --help      Show this help\n",
        name);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    name = argv[0];
    
    if (argc < 4) {
        print_usage();
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            print_usage();
        }
    }

    mount(argv[3], argv[2], argv[1], 0);
}