#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

int main(int argc, char **argv) {
    char *wm = argc > 1 ? argv[1] : "twm";
    setenv("DISPLAY", ":0", 1);

    pid_t xpid = fork();
    if (xpid == 0) {
        execlp("X", "X", "-logfile", "/tmp/X.log", NULL);
        _exit(1);
    }

    struct stat st;
    time_t start = time(NULL);

    for (;;) {
        if (stat("/tmp/.X11-unix/X0", &st) == 0 && S_ISSOCK(st.st_mode))
            break;
        if (time(NULL) - start > 15) {
            fprintf(stderr, "timeout waiting for X");
            exit(EXIT_FAILURE);
        }
        usleep(100000);
    }

    system("stty -echo -echoctl -isig -icanon min 0 time 0");

    pid_t wmpid = fork();
    if (wmpid == 0) {
        int fd = open("/tmp/wm.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 1);
        dup2(fd, 2);
        execlp(wm, wm, NULL);
        _exit(1);
    }

    printf("\033[H\033[J");
    fflush(stdout);

    waitpid(wmpid, NULL, 0);
    waitpid(xpid, NULL, 0);
    return 0;
}