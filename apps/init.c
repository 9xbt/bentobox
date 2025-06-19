#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#define HOME "/root"

int main(int argc, char *argv[]) {
    struct utsname sysinfo;

    if (uname(&sysinfo) == -1) {
        perror("uname");
    } else {
        printf("\nWelcome to \033[96mbentobox\033[0m!\n%s %s\n\n",
        sysinfo.sysname, sysinfo.version);
    }

    FILE *fptr;
    char hostname[256];
    if (!(fptr = fopen("/etc/hostname", "r")) ||
        !fgets(hostname, sizeof hostname, fptr) ||
        sethostname(hostname, strlen(hostname)) != 0) {
        perror("init: failed to set hostname");
    } else {
        fclose(fptr);
    }

    chdir(HOME);

    for (;;) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            char *arg[] = { "/usr/bin/bash", NULL };
            char *envp[] = { "HOME=" HOME, "TERM=xterm-256color", NULL };
            execve(arg[0], arg, envp);
            perror("execvp");
            exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }
    return -1;
}