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
    char *action = "Halt the system.";
    if (!strcmp(name, "shutdown"))
        action = "Shut down the system.";
    else if (!strcmp(name, "poweroff"))
        action = "Power off the system.";
    else if (!strcmp(name, "reboot"))
        action = "Reboot the system.";

    printf(
        "Usage: %s [OPTIONS...]\n"
        "%s\n"
        "\n"
        "Options:\n"
        "     --help      Show this help\n"
        "  -p --poweroff  Switch off the machine\n"
        "  -r --reboot    Reboot the machine\n",
        name, action);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    name = argv[0];

    if (argc < 2) {
        if (!strcmp(name, "shutdown") || !strcmp(name, "poweroff"))
            shutdown();
        else if (!strcmp(name, "reboot"))
            reboot();
        else
            print_usage();
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help"))
            print_usage();
    }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--poweroff"))
            shutdown();
        else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--reboot"))
            reboot();
        else {
            printf("%s: invalid option -- '%s'\n", name, argv[i]);
            print_usage();
        }
    }
}