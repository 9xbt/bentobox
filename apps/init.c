#include <stdio.h>
#include <sys/utsname.h>

int main(int argc, char *argv[]) {
    FILE *console = fopen("/dev/console", "w");

    fprintf(console, "\033[93minit:\033[0m Hello, world!\n");

    printf("\nWelcome to \033[96mbentobox\033[0m!\n");

    struct utsname sysinfo;
    if (uname(&sysinfo) == -1) {
        perror("uname");
    } else {
        printf("%s %s %s\n\n",
        sysinfo.sysname, sysinfo.release, sysinfo.version);
    }

    return 0;
}