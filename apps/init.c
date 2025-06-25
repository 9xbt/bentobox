#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#define HOME "/root"
#define HOSTNAME "/etc/hostname"

#ifdef __x86_64__
#define ARCH "x86_64"
#else
#define ARCH "unknown"
#endif

int main(int argc, char *argv[]) {
    //printf("\n  \033[97mStarting up \033[94mbentobox ("ARCH")\033[0m\n\n");

    FILE *fptr;
    char hostname[256];
    if (!(fptr = fopen(HOSTNAME, "r")) ||
        !fgets(hostname, sizeof hostname, fptr) ||
        sethostname(hostname, strlen(hostname)) != 0) {
        //printf(" \033[91m*\033[97m Failed to set hostname\033[0m\n");
    } else {
        //printf(" \033[92m*\033[97m Updated hostname from "HOSTNAME"\033[0m\n");
        fclose(fptr);
    }

    chdir(HOME);

    struct utsname sysinfo;
    if (uname(&sysinfo) == -1) {
        perror("uname");
    } else {
        printf("\nWelcome to \033[96mbentobox\033[0m!\n%s %s %s\n\n",
        sysinfo.sysname, sysinfo.release, sysinfo.version);
    }

    for (;;) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            char *arg[] = { "/usr/bin/bash-musl", NULL };
            char *envp[] = { "HOME=" HOME, "TERM=linux", NULL };
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