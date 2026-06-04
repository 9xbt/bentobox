#include <abi-bits/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <poll.h>

void mount(const char *path, const char *type, const char *device, long flags) {
    errno = -__syscall4(SYS_mount, (long)path, (long)type, (long)device, flags);
    if (errno) {
        perror("mount");
        exit(EXIT_FAILURE);
    }
}

char shell[256];
char shell_name[256];

pid_t spawn_serial_shell(void) {
    if (access("/dev/ttyS0", F_OK) != 0)
        return 0;

    char *envp[] = { "TERM=xterm", "HOME=/root", NULL };
    char *argv[] = { shell, NULL };

    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/ttyS0", O_RDWR);
        if (fd < 0)
            exit(ENOENT);
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);

        printf("Press enter to enable this TTY.");
        fflush(stdout);
        getchar();

        execve(shell, argv, envp);
        perror(shell);
        exit(errno);
    } else {
        return pid;
    }
}

pid_t spawn_shell(void) {
    char *envp[] = { "TERM=linux", "HOME=/root", NULL };
    char *argv[] = { shell, NULL };

    pid_t pid = fork();
    if (pid == 0) {
        execve(shell, argv, envp);
        perror(shell);
        exit(errno);
    }
}

int main(int argc, char *argv[]) {
    FILE *console = fopen("/dev/console", "w");

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_BLOCK, &set, NULL);

    fprintf(console, "\033[93minit:\033[0m mounting filesystems\n");
    umask(0);
    mkdir("/tmp", 0777);
    mkdir("/proc", 0755);
    umask(022);
    mount("/tmp", "tmp", "tmp", 0);
    mount("/proc", "proc", "proc", 0);

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

    setpwent();
    struct passwd *pw = getpwent();
    if (!pw) {
        fprintf(console, "\033[93minit:\033[0m failed to read /etc/passwd!\n");
        exit(1);
    }
    strcpy(shell, pw->pw_shell);
    
    char *path = strrchr(pw->pw_shell, '/');
    path = path ? path + 1 : pw->pw_shell;
    snprintf(shell_name, sizeof shell, "-%s", path);

    chdir("/root");

    printf("\nWelcome to \033[96mbentobox\033[0m!\n");

    struct utsname sysinfo;
    if (uname(&sysinfo) == -1) {
        perror("uname");
    } else {
        printf("%s %s %s\n\n",
        sysinfo.sysname, sysinfo.release, sysinfo.version);
    }

    pid_t pids[2];
    pids[0] = spawn_shell();
    pids[1] = spawn_serial_shell();

    int status;
    for (;;) {
        pid_t dead = wait(&status);
        if (errno == ECHILD)
            break;
        if (dead < 0) {
            perror("wait");
            continue;
        }

        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
            continue;

        if (dead == pids[0]) {
            pids[0] = spawn_shell();
        } else if (dead == pids[1]) {
            pids[1] = spawn_serial_shell();
        }
    }

    for (;;) {
        poll(NULL, 0, -1);
    }
    return -1;
}