#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

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

    for (;;) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            char *argv[] = { "/usr/bin/bash", NULL };
            char *envp[] = { "TERM=linux", "HOME=/root", NULL };

            execve(argv[0], argv, envp);
            perror(argv[0]);
            exit(errno);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return 0;
}