#include <abi-bits/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

char *name;

void umount(const char *path, long flags) {
    if (access(path, F_OK)) {
        fprintf(stderr, "%s: ", name);
        perror(path);
        exit(EXIT_FAILURE);
    }

    errno = -__syscall2(SYS_umount, (long)path, flags);
    if (errno) {
        perror(name);
        exit(EXIT_FAILURE);
    }
}

void print_usage(void) {
    printf(
        "Usage: %s [OPTIONS...] DIRECTORY\n"
        "Unmount a filesystem\n"
        "\n"
        "Options:\n"
        "     --help      Show this help\n",
        name);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    name = argv[0];
    
    if (argc < 2) {
        print_usage();
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            print_usage();
        }
    }

    umount(argv[1], 0);
}