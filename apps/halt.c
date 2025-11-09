#include <abi-bits/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

char *name;

void reboot(void) {
    errno = -__syscall0(SYS_reboot);
    if (errno) {
        perror(name);
        exit(EXIT_FAILURE);
    }
}

void shutdown(void) {
    errno = -__syscall0(SYS_shutdown);
    if (errno) {
        perror(name);
        exit(EXIT_FAILURE);
    }
}

void print_usage(void) {
    printf(
        "Usage: %s [OPTIONS...]\n"
        "Halt the system.\n"
        "\n"
        "Options:\n"
        "     --help      Show this help\n"
        "  -p --poweroff  Switch off the machine\n"
        "  -r --reboot    Reboot the machine\n",
        name);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    name = argv[0];

    if (argc < 2)
        print_usage();

    for (int i = argc - 1; i >= 1; i--) {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--poweroff"))
            shutdown();
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--reboot"))
            reboot();
        else if (!strcmp(argv[i], "--help"))
            print_usage();
        else {
            printf("%s: invalid option -- '%s'\n", name, argv[i]);
            print_usage();
        }
    }
}