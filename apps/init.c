#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    FILE *console = fopen("/dev/console", "w");

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_BLOCK, &set, NULL);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    char *envp[] = { "TERM=linux", "HOME=/root", NULL };
    if (pid == 0) {
        char *argv[] = { "/etc/rc", NULL };

        fprintf(console, "\033[93minit:\033[0m running script '%s'\n", argv[0]);
        execve(argv[0], argv, envp);
        perror(argv[0]);
        exit(errno);
    } else {
        int status;
        for (;;) {
            if (waitpid(pid, &status, 0) != pid)
                continue;
            if (WEXITSTATUS(status) == ENOEXEC)
                exit(EXIT_FAILURE);
            break;
        }
    }

    FILE *fptr = fopen("/etc/hostname", "r");
    char hostname[65];
    if (!fptr ||
        !fgets(hostname, sizeof hostname, fptr)) {
    } else {
        char *newline;
        if ((newline = strchr(hostname, '\n')))
            *newline = '\0';

        fprintf(console, "\033[93minit:\033[0m setting hostname to '%s'\n", hostname);
        if (sethostname(hostname, strlen(hostname))) {
            perror("sethostname");
        }
        fclose(fptr);
    }

    chdir("/root");

    printf("\nWelcome to \033[96mbentobox\033[0m!\n");

    struct utsname sysinfo;
    if (uname(&sysinfo) == -1) {
        perror("uname");
    } else {
        printf("%s %s %s\n\n",
        sysinfo.sysname, sysinfo.release, sysinfo.version);
    }

    if (fork() == 0) {
        int fd = open("/dev/ttyS0", O_RDWR);
        if (fd < 0)
            exit(0);
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        envp[0] = "TERM=xterm";

        printf("Press enter to enable this TTY.");
        fflush(stdout);
        getchar();
    }

    for (;;) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) {
            char *argv[] = { "/usr/bin/bash", NULL };

            execve(argv[0], argv, envp);
            perror(argv[0]);
            exit(errno);
        } else {
            int status;
            for (;;) {
                if (waitpid(pid, &status, 0) != pid)
                    continue;
                if (WEXITSTATUS(status) == ENOEXEC)
                    exit(EXIT_FAILURE);
                break;
            }
        }
    }

    return 0;
}