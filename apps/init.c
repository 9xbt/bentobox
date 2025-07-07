#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>

int main(int argc, char *argv[]) {
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        perror("sigprocmask");
        return 1;
    }

    FILE *fptr;
    char hostname[256];
    if (!(fptr = fopen("/etc/hostname", "r")) ||
        !fgets(hostname, sizeof hostname, fptr) ||
        sethostname(hostname, strlen(hostname)) != 0) {
    } else {
        fclose(fptr);
    }

    setpwent();
    struct passwd *pw = getpwent();

    char *home = malloc(strlen(pw->pw_dir) + 1);
    char *shell = malloc(strlen(pw->pw_shell) + 1);
    strcpy(home, pw->pw_dir);
    strcpy(shell, pw->pw_shell);

    chdir(pw->pw_dir);
    endpwent();

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
            char *env_pwd = malloc(strlen("PWD=") + strlen(home) + 1);
            char *env_home = malloc(strlen("HOME=") + strlen(home) + 1);
            strcpy(env_pwd, "PWD=");
            strcpy(env_home, "HOME=");
            strcpy(env_pwd + strlen("PWD="), home);
            strcpy(env_home + strlen("HOME="), home);

            char *arg[] = { shell, NULL };
            char *envp[] = { env_pwd, env_home, "TERM=linux", NULL };
            execve(arg[0], arg, envp);
            perror(shell);
            exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
            if (status != 0) exit(status);
        }
    }
    return -1;
}