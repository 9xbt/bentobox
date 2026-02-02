#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <spawn.h>
#include <fcntl.h>

extern char **environ;

int wait_for_x() {
    struct stat st;
    while (stat("/tmp/.X11-unix/X0", &st) < 0) usleep(50000);
    
    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strcpy(addr.sun_path, "/tmp/.X11-unix/X0");
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 &&
        errno != EINPROGRESS) {
        struct pollfd pfd = {sock, POLLOUT, 0};
        if (poll(&pfd, 1, 10000) <= 0) {
            close(sock);
            return -1;
        }
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error) {
            close(sock);
            return -1;
        }
    }
    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    pid_t xorg_pid, wm_pid, xkb_pid;
    char *x_argv[] = { "Xorg", "-retro", NULL};
    char *wm_argv[] = { argc < 2 ? "twm" : argv[1], NULL};
    
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    
    int logfd = open("/var/log/startwm.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, logfd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, logfd, STDERR_FILENO);
    
    posix_spawnp(&xorg_pid, "Xorg", &actions, NULL, x_argv, environ);
    
    if (wait_for_x() < 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(logfd);
        return 1;
    }

    setenv("DISPLAY", ":0", 1);
    posix_spawnp(&wm_pid, wm_argv[0], &actions, NULL, wm_argv, environ);
    
    posix_spawn_file_actions_destroy(&actions);
    close(logfd);
    
    if (!(xkb_pid = fork())) {
        int logfd = open("/var/log/startwm.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        close(logfd);
        
        FILE *p = popen("xkbcomp -w 0 -I/usr/share/X11/xkb - :0", "w");
        fprintf(p, "xkb_keymap {\n"
            "    xkb_keycodes  { include \"evdev+aliases(qwerty)\" };\n"
            "    xkb_types     { include \"complete\" };\n"
            "    xkb_compat    { include \"complete\" };\n"
            "    xkb_symbols   { include \"pc+us+inet(evdev)\" };\n"
            "    xkb_geometry  { include \"pc(pc105)\" };\n"
            "};\n");
        pclose(p);
        exit(0);
    }

    struct termios tio;
    if (tcgetattr(STDIN_FILENO, &tio) == -1)
        goto error;

    tio.c_lflag &= ~ISIG;

    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
    
error:
    waitpid(xkb_pid, NULL, 0);
    waitpid(wm_pid, NULL, 0);
    waitpid(xorg_pid, NULL, 0);
    return 0;
}