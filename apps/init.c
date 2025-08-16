#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <syslog.h>

int dprintf(int level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0};

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    int ret = vsprintf(buf + sprintf(buf, "\033[32m[%5lu.%06lu]\033[0m ", ts.tv_sec, ts.tv_nsec / 1000), fmt, args);
    fwrite(buf, 1, strlen(buf), stdout);

    va_end(args);
    return ret;
}

int main(int argc, char *argv[]) {
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGINT);

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        perror("sigprocmask");
        return 1;
    }

    struct stat st;
    if (!stat("/proc", &st) && !stat("/proc/mounts", &st)) {
        dprintf(LOG_INFO, "apps/%s:%d: /proc is already mounted\n", __FILE__, __LINE__);
    } else {
        dprintf(LOG_INFO, "apps/%s:%d: mounting /proc\n", __FILE__, __LINE__);
        mkdir("/proc", 0555);
        mount("proc", "/proc", "proc", 0, NULL);
    }

    FILE *fptr;
    char hostname[256];
    if (!(fptr = fopen("/etc/hostname", "r")) ||
        !fgets(hostname, sizeof hostname, fptr)) {
    } else {
        char *newline;
        if ((newline = strchr(hostname, '\n')))
            *newline = '\0';

        dprintf(LOG_INFO, "apps/%s:%d: setting hostname to '%s'\n", __FILE__, __LINE__, hostname);

        if (sethostname(hostname, strlen(hostname))) {
            perror("sethostname");
        }

        fclose(fptr);
    }

    setpwent();
    struct passwd *pw = getpwent();

    char *home = malloc(strlen(pw->pw_dir) + 1);
    //char *shell = malloc(strlen(pw->pw_shell) + 1);
    char *shell = "/bin/login";
    strcpy(home, pw->pw_dir);
    //strcpy(shell, pw->pw_shell);

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
            exit(-ENOEXEC);
        } else {
            int status;
            waitpid(pid, &status, 0);
            if (status == -ENOEXEC) exit(status);
        }
    }
    return -1;
}